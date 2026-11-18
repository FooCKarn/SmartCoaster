#!/usr/bin/env python3
"""
ai/train.py — เทรนโมเดล AI ของ SmartCoaster แล้วเขียนไฟล์ gateway/ai_model.h

โมเดล = logistic regression (tabular) ทำนายว่า "ชั่วโมงถัดไปผู้ใช้จะดื่มน้ำไม่พอ" (ความเสี่ยง 0..1)
gateway ใช้ค่าความเสี่ยงนี้ตัดสินใจยกป้ายด้วยเซอร์โว + ส่ง LINE (closed loop)

ใช้แค่ numpy:  pip install numpy

วิธีใช้
  1) ยังไม่มีข้อมูลจริง:
       python ai/make_dataset.py                                   # สร้าง dataset ตามงานวิจัย (ai/data/)
       python ai/train.py --csv ai/data/office_hydration_v1.csv    # ← ค่าเริ่มต้นที่ใช้อยู่
     (แบบเก่า: python ai/train.py --synthetic จำลองง่าย ๆ 60 วัน)

  2) มีข้อมูลจริงแล้ว (หลังเปิดระบบเก็บข้อมูลหลายวัน):
       Firebase console → Realtime Database → ⋮ → Export JSON  (ได้ไฟล์ เช่น smartcoaster-export.json)
       python ai/train.py --firebase smartcoaster-export.json
     ถ้าข้อมูลจริงยังน้อย เติมข้อมูลจำลองผสมได้:
       python ai/train.py --firebase smartcoaster-export.json --mix-synthetic

  ผลลัพธ์ (accuracy / precision / recall / F1 + confusion matrix) พิมพ์ออกจอ ใช้ใส่ report หัวข้อ AI accuracy ได้
  แล้วคัดลอก gateway/ai_model.h ที่ได้ไปอัปโหลดลง gateway ใหม่

ลำดับ feature ต้องตรงกับ computeFeatures() ใน gateway/gateway.ino (gateway บันทึก ai_x ไว้ทุก reading)
"""
import argparse
import json
import math
import os
import sys

import numpy as np

FEATURES = [
    "mins_since_drink",  # นาทีตั้งแต่ดื่มครั้งล่าสุด (ตัดที่ 240)
    "ml_last_hour",      # ปริมาณที่ดื่มใน 60 นาทีที่ผ่านมา
    "ml_today",          # ปริมาณสะสมวันนี้
    "room_t",            # อุณหภูมิห้อง °C (DHT11)
    "room_rh",           # ความชื้น % (DHT11)
    "present",           # มีคนนั่งอยู่ (PIR หรือ ultrasonic < 80 cm) 0/1
    "sound_p2p",         # ระดับเสียงห้อง (KY-038, peak-to-peak ADC)
]
HORIZON_MIN = 60       # มองไปข้างหน้า 60 นาที
# ควรดื่มอย่างน้อยเท่านี้ใน 1 ชม.: EFSA แนะนำน้ำรวม 2.0 L (หญิง) / 2.5 L (ชาย) ต่อวัน ~70–80 % มาจากเครื่องดื่ม
# = 1.5–2.0 L ตลอด ~16 ชม. ที่ตื่น ≈ 95–125 ml/ชม. ใช้ 100 (ดู ai/data/SOURCES.md)
DEFAULT_NEED_ML = 100


def need_ml(room_t, base=DEFAULT_NEED_ML):
    """ปริมาณที่ควรดื่มใน 1 ชม. ห้องร้อนต้องดื่มมากขึ้น"""
    t = 26.0 if room_t is None or math.isnan(room_t) else room_t
    return base + 10.0 * max(0.0, t - 28.0)


# ---------------------------------------------------------------- data: synthetic
def simulate(days=60, seed=1):
    """จำลองพฤติกรรมการดื่มน้ำ 08:00–20:00 คืนค่า X, y, t (นาทีนับจากเริ่ม)"""
    rng = np.random.default_rng(seed)
    X, y, T = [], [], []
    for d in range(days):
        day_t = rng.uniform(25, 33)
        day_rh = rng.uniform(45, 80)
        minutes = 12 * 60
        present = np.zeros(minutes, bool)
        busy = np.zeros(minutes, bool)
        p, b = True, False
        for m in range(minutes):
            if p and rng.random() < 1 / 100: p = False      # ลุกไปไหน ~ทุก 100 นาที
            elif not p and rng.random() < 1 / 30: p = True  # ไป ~30 นาที
            if b and rng.random() < 1 / 80: b = False       # ช่วงยุ่ง/โฟกัสยาว ~80 นาที
            elif not b and rng.random() < 1 / 80: b = True
            present[m], busy[m] = p, b
        temp = day_t + 2 * np.sin(np.arange(minutes) / minutes * np.pi) + rng.normal(0, 0.3, minutes)
        rh = day_rh + rng.normal(0, 2, minutes)
        sound = np.where(busy, 380, 240) + rng.normal(0, 150, minutes)   # เสียงบอกความยุ่งได้แค่คร่าว ๆ

        drinks = np.zeros(minutes)
        since = 60.0
        for m in range(minutes):
            since += 1
            if present[m]:
                # ยิ่งไม่ได้ดื่มนาน ยิ่งลืม (นี่คือปัญหาที่ป้ายเตือนแก้), ยุ่ง/โฟกัส (ห้องเสียงดัง) ก็ลืม
                # ห้องร้อนดื่มบ่อยขึ้นเล็กน้อย
                h = ((1 / 12) * (0.35 if busy[m] else 1.0) * (1 + 0.04 * (temp[m] - 28))
                     * (0.25 + 0.75 * np.exp(-since / 45)))
                if rng.random() < h:
                    drinks[m] = rng.uniform(40, 120)
                    since = 0

        since = 120.0
        today = 0.0
        hist = []
        for m in range(minutes):
            if drinks[m] > 0:
                since = 0; today += drinks[m]; hist.append((m, drinks[m]))
            else:
                since += 1
            if m % 5 or m + HORIZON_MIN >= minutes:
                continue
            last_hour = sum(a for (tm, a) in hist if m - tm < 60)
            pres = 1.0 if present[max(0, m - 5):m + 1].any() else 0.0
            X.append([min(since, 240), last_hour, today, temp[m], rh[m], pres, max(sound[m], 0)])
            future = drinks[m + 1:m + 1 + HORIZON_MIN].sum()
            y.append(1.0 if future < need_ml(temp[m]) else 0.0)
            T.append(d * 1440 + m)
    return np.array(X, float), np.array(y, float), np.array(T, float)


# ---------------------------------------------------------------- data: Firebase export
def load_firebase(path):
    with open(path, encoding="utf-8") as f:
        db = json.load(f)
    readings = [r for r in (db.get("readings") or {}).values()
                if isinstance(r, dict) and isinstance(r.get("ai_x"), list)
                and len(r["ai_x"]) == len(FEATURES) and not r.get("demo") and r.get("ts")]
    drinks = sorted((float(v["ts"]), float(v.get("ml", 0)))
                    for v in (db.get("drinks") or {}).values()
                    if isinstance(v, dict) and v.get("ts") and not v.get("demo"))
    if not readings:
        sys.exit("ไม่พบ readings ที่มี ai_x ในไฟล์ export (gateway เวอร์ชันใหม่ต้องรันเก็บข้อมูลก่อน และต้องปิด DEMO_SPEED)")
    readings.sort(key=lambda r: r["ts"])
    last_ts = readings[-1]["ts"]
    X, y, T = [], [], []
    dts = np.array([d[0] for d in drinks]) if drinks else np.zeros(0)
    dml = np.array([d[1] for d in drinks]) if drinks else np.zeros(0)
    for r in readings:
        ts = float(r["ts"])
        if ts + HORIZON_MIN * 60000 > last_ts:
            break  # ยังไม่รู้อนาคตครบ 1 ชม.
        x = [float("nan") if v is None else float(v) for v in r["ai_x"]]
        if any(math.isnan(v) for v in x):
            continue
        mask = (dts > ts) & (dts <= ts + HORIZON_MIN * 60000)
        future = dml[mask].sum()
        X.append(x)
        y.append(1.0 if future < need_ml(x[3]) else 0.0)
        T.append(ts / 60000)
    print(f"Firebase: readings ที่ใช้ได้ {len(X)} จุด, drinks {len(drinks)} ครั้ง")
    return np.array(X, float), np.array(y, float), np.array(T, float)


# ---------------------------------------------------------------- data: CSV จาก ai/make_dataset.py
def load_csv(path):
    import csv
    X, y, T = [], [], []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            x = [float(r[n]) for n in FEATURES]
            X.append(x)
            y.append(1.0 if float(r["future_ml_60"]) < need_ml(x[3]) else 0.0)
            hh, mm = r["time"].split(":")
            # เรียงตามคน แล้ววัน แล้วเวลา → แบ่ง 80/20 = test เป็น "คนที่โมเดลไม่เคยเห็น"
            T.append(int(r["person"]) * 100000 + int(r["day"]) * 1440 + int(hh) * 60 + int(mm))
    print(f"CSV {path}: {len(X)} แถว")
    return np.array(X, float), np.array(y, float), np.array(T, float)


# ---------------------------------------------------------------- model
def fit_logreg(X, y, l2=1e-2, lr=0.2, epochs=3000):
    mean, std = X.mean(0), X.std(0)
    std[std < 1e-6] = 1.0
    Z = (X - mean) / std
    w = np.zeros(X.shape[1]); b = 0.0
    n = len(y)
    for _ in range(epochs):
        p = 1 / (1 + np.exp(-(Z @ w + b)))
        g = p - y
        w -= lr * (Z.T @ g / n + l2 * w)
        b -= lr * g.mean()
    return mean, std, w, b


def predict(model, X):
    mean, std, w, b = model
    return 1 / (1 + np.exp(-(((X - mean) / std) @ w + b)))


def report(name, y, p, thr=0.5):
    yhat = p >= thr
    tp = int(((yhat == 1) & (y == 1)).sum()); tn = int(((yhat == 0) & (y == 0)).sum())
    fp = int(((yhat == 1) & (y == 0)).sum()); fn = int(((yhat == 0) & (y == 1)).sum())
    acc = (tp + tn) / max(len(y), 1)
    prec = tp / max(tp + fp, 1); rec = tp / max(tp + fn, 1)
    f1 = 2 * prec * rec / max(prec + rec, 1e-9)
    print(f"  {name:<28} acc={acc:.3f}  precision={prec:.3f}  recall={rec:.3f}  F1={f1:.3f}"
          f"   [TP={tp} FP={fp} FN={fn} TN={tn}]")
    return acc


def write_header(model, path, source, n_train, test_acc):
    mean, std, w, b = model
    arr = lambda a: ", ".join(f"{v:.6g}f" for v in a)
    names = ", ".join(f'"{n}"' for n in FEATURES)
    src = f"""// gateway/ai_model.h — สร้างอัตโนมัติโดย ai/train.py (อย่าแก้มือ ให้รัน train.py ใหม่)
// โมเดล: logistic regression ทำนายความเสี่ยงว่า {HORIZON_MIN} นาทีข้างหน้าจะดื่มน้ำไม่พอ
// ข้อมูลที่ใช้เทรน: {source}  ({n_train} ตัวอย่าง, test accuracy = {test_acc:.3f})
#pragma once
#include <math.h>

#define AI_N_FEATURES {len(FEATURES)}
static const char *const AI_FEATURE_NAMES[AI_N_FEATURES] = {{{names}}};
static const float AI_MEAN[AI_N_FEATURES] = {{{arr(mean)}}};
static const float AI_STD[AI_N_FEATURES]  = {{{arr(std)}}};
static const float AI_W[AI_N_FEATURES]    = {{{arr(w)}}};
static const float AI_B = {b:.6g}f;
static const char *const AI_TRAINED_ON = "{source}";

// คืนค่าความเสี่ยง 0..1 (x ต้องเรียงตาม AI_FEATURE_NAMES)
static inline float aiPredict(const float *x) {{
  float z = AI_B;
  for (int i = 0; i < AI_N_FEATURES; i++) z += AI_W[i] * (x[i] - AI_MEAN[i]) / AI_STD[i];
  return 1.0f / (1.0f + expf(-z));
}}
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"\nเขียน {path} แล้ว → อัปโหลด gateway ใหม่")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--synthetic", action="store_true", help="ใช้ข้อมูลจำลอง")
    ap.add_argument("--firebase", metavar="EXPORT_JSON", help="ไฟล์ Export JSON จาก Firebase RTDB")
    ap.add_argument("--csv", metavar="CSV", help="dataset จาก ai/make_dataset.py เช่น ai/data/office_hydration_v1.csv")
    ap.add_argument("--mix-synthetic", action="store_true", help="ผสมข้อมูลจำลองเข้ากับข้อมูลจริง")
    ap.add_argument("--days", type=int, default=60, help="จำนวนวันที่จำลอง (default 60)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "gateway", "ai_model.h"))
    a = ap.parse_args()
    if not a.synthetic and not a.firebase and not a.csv:
        ap.error("เลือก --csv <dataset.csv>, --synthetic หรือ --firebase <export.json>")

    parts, source = [], []
    if a.csv:
        parts.append(load_csv(a.csv)); source.append(os.path.basename(a.csv))
    if a.firebase:
        parts.append(load_firebase(a.firebase)); source.append("firebase")
    if a.synthetic or a.mix_synthetic:
        parts.append(simulate(a.days)); source.append(f"synthetic {a.days} days")
    X = np.vstack([p[0] for p in parts]); y = np.concatenate([p[1] for p in parts])
    T = np.concatenate([p[2] for p in parts])

    # แบ่ง train/test ตามเวลา (80% แรก = train) กันข้อมูลอนาคตรั่วเข้า train
    # (ถ้าใช้ --csv ลำดับคือ คน→วัน→เวลา test จึงเป็นคนที่ไม่เคยเห็นตอนเทรน)
    order = np.argsort(T, kind="stable"); X, y = X[order], y[order]
    cut = int(len(y) * 0.8)
    if cut < 20 or len(y) - cut < 5:
        sys.exit(f"ข้อมูลน้อยเกินไป ({len(y)} ตัวอย่าง) เก็บเพิ่มหรือใช้ --mix-synthetic")
    Xtr, ytr, Xte, yte = X[:cut], y[:cut], X[cut:], y[cut:]
    print(f"ตัวอย่างทั้งหมด {len(y)} (train {len(ytr)} / test {len(yte)}), สัดส่วน 'ดื่มไม่พอ' = {y.mean():.2f}")

    model = fit_logreg(Xtr, ytr)
    print("\nผลบนชุด test:")
    acc = report("logistic regression (AI)", yte, predict(model, Xte))
    report("เดาค่าที่พบบ่อยสุดเสมอ", yte, np.full(len(yte), float(ytr.mean() >= 0.5)))
    report("กฎเดิม: ไม่ดื่ม > 45 นาที", yte, (Xte[:, 0] > 45).astype(float))

    print("\nน้ำหนักของแต่ละ feature (หลัง standardize, + = เพิ่มความเสี่ยง):")
    for n, wi in sorted(zip(FEATURES, model[2]), key=lambda t: -abs(t[1])):
        print(f"  {n:<18} {wi:+.3f}")
    write_header(model, os.path.normpath(a.out), " + ".join(source), len(ytr), acc)


if __name__ == "__main__":
    main()
