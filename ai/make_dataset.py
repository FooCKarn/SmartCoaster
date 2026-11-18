#!/usr/bin/env python3
"""
ai/make_dataset.py — สร้าง dataset การดื่มน้ำของคนทำงานโต๊ะ (จำลอง แต่ตั้งค่าตามงานวิจัย)

ยังไม่มีข้อมูลจริงจากแก้ว ไฟล์นี้จำลอง "หลายคน × หลายวันทำงาน" ทีละนาที โดยใช้ตัวเลขจากงานวิจัย
ที่ค้นได้ (ดู ai/data/SOURCES.md) แล้วเก็บตัวอย่างทุก 5 นาที ในรูปแบบเดียวกับที่ gateway บันทึก
(ai_x 7 ค่า) + ปริมาณที่ดื่มจริงใน 60 นาทีถัดไป (ไว้ทำ label)

    pip install numpy
    python ai/make_dataset.py                 # 24 คน × 20 วัน → ai/data/
    python ai/make_dataset.py --people 40 --days 30 --seed 7

ไฟล์ที่ได้
    ai/data/office_hydration_v1.csv   แถวละ 5 นาที: feature 7 ตัว + weight_g + future_ml_60 + label
    ai/data/drinks_v1.csv             การดื่มแต่ละครั้ง (เวลา, ml)
    ai/data/people_v1.csv             ค่าประจำตัวของแต่ละคนที่สุ่มมา

เทรน:  python ai/train.py --csv ai/data/office_hydration_v1.csv
"""
import argparse
import csv
import math
import os

import numpy as np

# ---------------------------------------------------------------- ค่าจากงานวิจัย (ดู SOURCES.md)
DAILY_DRINK_MEDIAN_ML = 1556     # [S1] คนทำงาน median น้ำเปล่า 1,556 ml/วัน
DAILY_DRINK_IQR = (1473, 2433)   # [S1] IQR ของ total fluid (ใช้กำหนดความกว้างการกระจาย)
SWALLOW_ML_MEAN = 16             # [S2] ขนาดจิบน้ำตามธรรมชาติ เฉลี่ย ~16 ml
ROOM_T_MEAN, ROOM_T_SD = 26.35, 0.96   # [S4] ห้องแอร์ในไทย อุณหภูมิเฉลี่ย 26.35 °C (23.9–28.8)
ROOM_RH_MEAN, ROOM_RH_SD = 57.19, 5.62 # [S4] RH เฉลี่ย 57.2 % (41–71)
TEMP_EFFECT_PER_C = 5.69 / 1556  # [S5] ดื่มเพิ่ม ~5.69 g/วัน ต่อ 1 °C (≈0.4 %/°C, ผลน้อยมาก)

# ---------------------------------------------------------------- สมมติฐาน (ไม่มีตัวเลขอ้างอิง)
WORK_SHARE = 0.5          # สัดส่วนน้ำที่ดื่มระหว่างอยู่โต๊ะทำงาน 10 ชม. (ที่เหลือดื่มที่บ้าน/มื้ออาหาร)
DAY_START, DAY_END = 8 * 60, 18 * 60
HOT_DAY_PROB = 0.10       # วันที่แอร์ไม่เย็น/ปิด อุณหภูมิ +2..4 °C
SAMPLE_EVERY = 5          # gateway คำนวณ AI ถี่กว่านี้ แต่เก็บ dataset ทุก 5 นาทีพอ
HORIZON = 60

FEATURES = ["mins_since_drink", "ml_last_hour", "ml_today", "room_t", "room_rh", "present", "sound_p2p"]


def make_person(rng, pid):
    sigma = math.log(DAILY_DRINK_IQR[1] / DAILY_DRINK_IQR[0]) / 1.349    # IQR → sigma ของ lognormal
    daily = float(np.clip(rng.lognormal(math.log(DAILY_DRINK_MEDIAN_ML), sigma), 700, 3500))
    return dict(
        person=pid,
        daily_ml=round(daily),
        work_target_ml=round(daily * WORK_SHARE),
        swallow_ml=round(float(np.clip(rng.normal(SWALLOW_ML_MEAN + 2, 4), 9, 30)), 1),
        swallows_per_drink=round(float(rng.uniform(2.0, 4.5)), 2),   # สมมติฐาน: 1 ครั้งที่ยกแก้ว = กลืนหลายครั้ง
        forget_strength=round(float(rng.uniform(0.3, 0.85)), 2),     # สมมติฐาน: ไม่ได้ดื่มนาน → ลืมมากขึ้น
        forget_tau_min=round(float(rng.uniform(40, 120)), 1),
        busy_factor=round(float(rng.uniform(0.25, 0.7)), 2),         # สมมติฐาน: ช่วงยุ่งดื่มน้อยลง
        cup_ml=int(rng.choice([300, 350, 400, 500, 600])),
        cup_mass_g=int(rng.uniform(180, 380)),
        meetings_per_day=round(float(rng.uniform(0.3, 2.5)), 2),
    )


def simulate_day(rng, P, scale, record=True):
    n = DAY_END - DAY_START
    arrive = int(rng.integers(0, 60))
    leave = n - int(rng.integers(0, 60))
    present = np.zeros(n, bool); present[arrive:leave] = True
    if rng.random() < 0.85:                                   # พักเที่ยง
        s = 4 * 60 + int(rng.integers(-15, 15)); present[s:s + int(rng.integers(45, 75))] = False
    for _ in range(rng.poisson(P["meetings_per_day"])):       # ประชุม/ไปทำธุระ
        s = int(rng.integers(arrive, max(arrive + 1, leave - 30))); present[s:s + int(rng.integers(30, 90))] = False
    m = 0
    while m < n:                                              # ลุกไปเข้าห้องน้ำ/คุย สั้น ๆ
        m += int(rng.exponential(90))
        if m < n: present[m:m + int(rng.integers(3, 10))] = False

    busy = np.zeros(n, bool); b = False
    for i in range(n):
        if b and rng.random() < 1 / 45: b = False
        elif not b and rng.random() < 1 / 60: b = True
        busy[i] = b
    noisy = busy & (rng.random(n) < 0.6) | (rng.random(n) < 0.05)   # ช่วงยุ่งมักมีเสียงคุย/ประชุมออนไลน์
    sound = np.where(noisy, rng.normal(380, 140, n), rng.normal(130, 45, n)).clip(20, 1500)

    hot = rng.random() < HOT_DAY_PROB
    t0 = rng.normal(ROOM_T_MEAN, ROOM_T_SD * 0.7) + (rng.uniform(2, 4) if hot else 0)
    temp = t0 + 0.6 * np.sin(np.arange(n) / n * math.pi) + rng.normal(0, 0.15, n)
    rh = np.clip(rng.normal(ROOM_RH_MEAN, ROOM_RH_SD * 0.8) + rng.normal(0, 1.0, n) + (6 if hot else 0), 35, 85)

    mean_drink = P["swallow_ml"] * (1 + P["swallows_per_drink"])
    present_min = max(present.sum(), 60)
    base = P["work_target_ml"] / mean_drink / present_min * scale
    drinks = np.zeros(n)
    level = np.zeros(n)                          # น้ำในแก้ว (ml) ทุกนาที → ใช้จำลองค่าที่ load cell อ่านได้
    water = float(P["cup_ml"]) * rng.uniform(0.6, 1.0)
    since = float(rng.uniform(20, 120))         # ดื่มครั้งสุดท้ายที่บ้าน/ระหว่างทาง
    back_boost = 0
    for i in range(n):
        since += 1
        if i > 0 and present[i] and not present[i - 1]: back_boost = 10   # กลับมาที่โต๊ะ มักหยิบแก้ว
        if not present[i]: level[i] = water; continue
        h = base
        h *= P["busy_factor"] if busy[i] else 1.0
        h *= 1 + TEMP_EFFECT_PER_C * (temp[i] - ROOM_T_MEAN)                       # ≈ +0.4 %/°C
        h *= (1 - P["forget_strength"]) + P["forget_strength"] * math.exp(-since / P["forget_tau_min"])
        if back_boost > 0: h *= 2.0; back_boost -= 1
        level[i] = water
        if water > 20 and rng.random() < min(h, 0.5):
            k = 1 + rng.poisson(P["swallows_per_drink"])
            ml = float(min(water, sum(max(3, rng.normal(P["swallow_ml"], P["swallow_ml"] * 0.35)) for _ in range(k))))
            drinks[i] = ml; water -= ml; since = 0
        if water < 50 and present[i] and rng.random() < 0.15:  # น้ำใกล้หมด → ไปเติม
            water = float(P["cup_ml"])
        level[i] = water
    return dict(present=present, busy=busy, sound=sound, temp=temp, rh=rh, drinks=drinks, level=level,
                since0=since, arrive=arrive)


def calibrate(rng, P):
    """ปรับ scale ให้ปริมาณที่ดื่มเฉลี่ยใกล้ work_target_ml ของคนนั้น"""
    scale = 1.0
    for _ in range(4):
        got = np.mean([simulate_day(rng, P, scale)["drinks"].sum() for _ in range(6)])
        scale *= float(np.clip(P["work_target_ml"] / max(got, 1), 0.5, 3))
    return scale


def rows_for_day(rng, P, day, D, weight_noise=1.5):
    n = len(D["drinks"])
    rows, events = [], []
    since = None
    hist = []
    today = 0.0
    water = None
    cum = np.concatenate([[0], np.cumsum(D["drinks"])])
    last_seen_present = -999
    for i in range(n):
        if D["drinks"][i] > 0:
            hist.append((i, D["drinks"][i])); today += D["drinks"][i]; since = 0
            events.append(dict(person=P["person"], day=day, time=hhmm(i), ml=round(D["drinks"][i], 1)))
        elif since is not None:
            since += 1
        if D["present"][i]: last_seen_present = i
        if i % SAMPLE_EVERY or i + HORIZON >= n or i < D["arrive"]:
            continue
        # gateway รู้ "นาทีตั้งแต่ดื่ม" เฉพาะที่ดื่มบนแก้วนี้ ก่อนดื่มครั้งแรกของวันนับจากเปิดเครื่อง (~เวลามาถึง)
        s = since if since is not None else (i - D["arrive"]) + 0.0
        future = cum[i + 1 + HORIZON] - cum[i + 1]
        pres = 1 if i - last_seen_present <= 5 else 0
        rows.append(dict(
            person=P["person"], day=day, time=hhmm(i),
            mins_since_drink=round(min(s, 240), 1),
            ml_last_hour=round(sum(a for (t, a) in hist if i - t < 60), 1),
            ml_today=round(today, 1),
            room_t=float(np.round(D["temp"][i])),          # DHT11 ละเอียด 1 °C
            room_rh=float(np.round(D["rh"][i])),           # DHT11 ละเอียด 1 %
            present=pres,
            sound_p2p=int(D["sound"][i]),
            weight_g=round(P["cup_mass_g"] + D["level"][i] + rng.normal(0, weight_noise), 1),  # แก้ว + น้ำ (1 ml ≈ 1 g)
            busy=int(D["busy"][i]),
            future_ml_60=round(future, 1),
        ))
    return rows, events


def hhmm(i):
    m = DAY_START + i
    return f"{m // 60:02d}:{m % 60:02d}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--people", type=int, default=24)
    ap.add_argument("--days", type=int, default=20)
    ap.add_argument("--seed", type=int, default=2026)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "data"))
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    os.makedirs(a.out, exist_ok=True)

    people, rows, events = [], [], []
    for pid in range(1, a.people + 1):
        P = make_person(rng, pid)
        P["scale"] = round(calibrate(rng, P), 3)
        people.append(P)
        for d in range(1, a.days + 1):
            r, e = rows_for_day(rng, P, d, simulate_day(rng, P, P["scale"]))
            rows += r; events += e

    def dump(name, data):
        path = os.path.join(a.out, name)
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(data[0].keys()))
            w.writeheader(); w.writerows(data)
        print(f"เขียน {path}  ({len(data)} แถว)")

    dump("office_hydration_v1.csv", rows)
    dump("drinks_v1.csv", events)
    dump("people_v1.csv", people)

    per_day = {}
    for e in events: per_day[(e["person"], e["day"])] = per_day.get((e["person"], e["day"]), 0) + e["ml"]
    v = np.array(list(per_day.values())); n_ev = len(events) / max(len(per_day), 1)
    print(f"\nสรุป: ดื่มที่โต๊ะ median {np.median(v):.0f} ml/วัน (IQR {np.percentile(v,25):.0f}–{np.percentile(v,75):.0f}),"
          f" {n_ev:.1f} ครั้ง/วัน, ครั้งละเฉลี่ย {np.mean([e['ml'] for e in events]):.0f} ml")


if __name__ == "__main__":
    main()
