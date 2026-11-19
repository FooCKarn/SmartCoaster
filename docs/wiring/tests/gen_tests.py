#!/usr/bin/env python3
"""Per-sensor test wiring diagrams (tests/t01–t11). Uses helpers from ../gen.py style.

Run: python3 gen_tests.py <out_dir>   -> writes t0X-*.svg
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from gen import SVG, C, P, ESP_X, ESP_W, esp32, notes_box  # noqa: E402

W = 1700
MX, MW = 230, 250          # left module column
TOP = 120


def base(title, sub, used, board, usb_text="เสียบ USB เข้าคอมพิวเตอร์ (จ่ายไฟ + Serial 115200)", usb_color="#1b2a41"):
    s = SVG(W, 1300)
    s.text(40, 46, title, 26, "start", 700, "#1b2a41")
    s.text(40, 72, sub, 14, "start", 400, "#555")
    pts, y0, bot = esp32(s, TOP, used, board)
    s.text(ESP_X + ESP_W / 2, bot + 22, usb_text, 13, "middle", 700, usb_color)
    return s, pts, (lambda i: y0 + i * P), bot


def finish(s, bot, wires, expect, extra=None, expect_title="ผลที่ควรเห็นใน Serial Monitor (115200)"):
    lines = ["▸ ต่อสาย"] + [f"   {i+1}) {w}" for i, w in enumerate(wires)]
    lines += [f"▸ {expect_title}"] + [f"   • {e}" for e in expect]
    if extra:
        lines += ["▸ ข้อควรระวัง"] + [f"   • {e}" for e in extra]
    end = notes_box(s, 40, bot + 70, W - 80, "เช็กลิสต์ทดสอบ", lines)
    s.h = int(end + 30)
    return s


def left_box(s, r1, r2, title, sub, ry, fill="#eef4fb", stroke="#4a6fa5"):
    y1, y2 = ry(r1) - 24, ry(r2) + 24
    s.module(MX, y1, MW, y2 - y1, title, sub, fill=fill, stroke=stroke)
    return y1, y2


def to_left_pin(s, src, pin_pt, xr, color):
    """route from module left pin src=(x,y) out to x=xr, down/up to pin row, right to ESP32 left pin."""
    x, y = src
    px, py = pin_pt
    s.line([(x, y), (xr, y), (xr, py), (px, py)], color)


def to_right_pin_under(s, src, pin_pt, xr, yb, xr2, color):
    """left module pin -> out left to xr -> down to yb (under board) -> right to xr2 -> up to right pin row -> left to pin."""
    x, y = src
    px, py = pin_pt
    s.line([(x, y), (xr, y), (xr, yb), (xr2, yb), (xr2, py), (px, py)], color)


def sig(s, a, b, color, label):
    s.line([a, b], color)
    s.wire_label((a[0] + b[0]) / 2, a[1], label, color)


# ---------------------------------------------------------------- t01
def t01():
    s, pts, ry, bot = base("t01 · Blink + อ่าน MAC (ทำทั้ง 2 บอร์ด)",
                           "tests/t01_blink · ไม่ต้องต่ออะไรเพิ่ม · ใช้เช็กว่าบอร์ด/สาย USB/ไดรเวอร์ใช้ได้", {"D2": 1}, "ESP32 (บอร์ดไหนก็ได้)")
    x, y = pts["D2"]
    s.add(f'<circle cx="{x+60}" cy="{y}" r="16" fill="#3fa3ff" stroke="#1b5fa8" stroke-width="3"/>')
    s.line([(x, y), (x + 44, y)], "#1b5fa8", dash="5 4")
    s.text(x + 90, y + 5, "LED บนบอร์ด (GPIO2) ต้องกะพริบ", 14, "start", 700, "#1b5fa8")
    return finish(s, bot, ["ไม่มี เสียบ USB อย่างเดียว"],
                  ["Board OK. STA MAC = XX:XX:XX:XX:XX:XX  แล้วตามด้วย blink ทุกวินาที",
                   "จด MAC ของบอร์ด gateway ลง docs/calibration.md (node ต้องใช้ส่ง ESP-NOW)"],
                  ["อัปโหลดไม่ขึ้น: ลองกดปุ่ม BOOT ค้างตอนขึ้น Connecting... / เปลี่ยนสาย USB ที่ส่งข้อมูลได้"])


# ---------------------------------------------------------------- t02
def t02():
    used = {"D14": 1, "3V3": 1, "GNDL": 1}
    s, pts, ry, bot = base("t02 · DHT11 (KY-015) — บอร์ด Gateway",
                           "tests/t02_dht11 · DHT_PIN = 14 · อ่านทุก 2 วินาที", used, "GATEWAY")
    y1, y2 = left_box(s, 9.2, 11.2, "DHT11 KY-015", ["อุณหภูมิ/ความชื้น"], ry, "#fff6e8", "#d98200")
    sp = (MX + MW, ry(10)); s.mpin(*sp, "S", "right")
    sig(s, sp, pts["D14"], C["s6"], "S → GPIO14")
    vp = (MX, ry(9.6)); gp = (MX, ry(10.6))
    s.mpin(*vp, "+", "left"); s.mpin(*gp, "−", "left")
    to_right_pin_under(s, vp, pts["3V3"], MX - 70, bot + 44, ESP_X + ESP_W + 60, C["3v3"])
    to_left_pin(s, gp, pts["GNDL"], MX - 35, C["gnd"])
    return finish(s, bot,
                  ["DHT11 S → D14 (GPIO14)", "DHT11 + → 3V3", "DHT11 − → GND"],
                  ["DHT11 test แล้วตามด้วย  T = 27.0 C  RH = 60 %  ทุก 2 วินาที",
                   "ลองเป่าลมหายใจใส่ → RH ต้องขึ้น"],
                  ["ขึ้น DHT read FAILED ตลอด: เช็กสาย S และ + (ต้องเป็น 3V3) · ต้องติดตั้งไลบรารี DHT sensor library (Adafruit)"])


# ---------------------------------------------------------------- t03
def t03():
    used = {"D27": 1, "VIN": 1, "GNDL": 1}
    s, pts, ry, bot = base("t03 · PIR HC-SR501 (interrupt) — บอร์ด Gateway",
                           "tests/t03_pir · PIR_PIN = 27 · attachInterrupt RISING", used, "GATEWAY")
    left_box(s, 7.6, 10.2, "PIR HC-SR501", ["OUT = 3.3 V ต่อตรงได้", "jumper = H (repeat)"], ry, "#eefbf1", "#3c8d4f")
    op = (MX + MW, ry(9)); s.mpin(*op, "OUT", "right")
    sig(s, op, pts["D27"], C["s4"], "OUT → GPIO27")
    vp = (MX, ry(8.6)); gp = (MX, ry(9.6))
    s.mpin(*vp, "VCC", "left"); s.mpin(*gp, "GND", "left")
    to_left_pin(s, vp, pts["VIN"], MX - 70, C["5v"])
    to_left_pin(s, gp, pts["GNDL"], MX - 35, C["gnd"])
    return finish(s, bot,
                  ["PIR OUT → D27 (GPIO27)", "PIR VCC → VIN (5 V จาก USB)", "PIR GND → GND"],
                  ["PIR warming up 60 s... → PIR ready",
                   "โบกมือหน้าเซนเซอร์ → [xxxx ms] MOTION #1, #2, ...  และ OUT=1 ระหว่างมีการเคลื่อนไหว"],
                  ["ช่วง warm-up อาจทริกเองได้ ไม่ต้องตกใจ · trimmer Tx = เวลาค้าง OUT, Sx = ระยะ (หมุนทวนเข็มสุดก่อน)"])


# ---------------------------------------------------------------- t04
def t04():
    used = {"D35": 1, "D32": 1, "3V3": 1, "GNDL": 1}
    s, pts, ry, bot = base("t04 · ไมโครโฟน KY-038 — บอร์ด Gateway",
                           "tests/t04_sound · AO = GPIO35 (ADC1), DO = GPIO32 · วัด peak-to-peak", used, "GATEWAY")
    left_box(s, 3.4, 5.6, "เสียง KY-038", ["มี trimmer ปรับ DO"], ry)
    ap = (MX + MW, ry(4)); dp = (MX + MW, ry(5))
    s.mpin(*ap, "AO", "right"); s.mpin(*dp, "DO", "right")
    sig(s, ap, pts["D35"], C["s2"], "AO → GPIO35")
    sig(s, dp, pts["D32"], C["s3"], "DO → GPIO32")
    vp = (MX, ry(4)); gp = (MX, ry(5))
    s.mpin(*vp, "+", "left"); s.mpin(*gp, "G", "left")
    to_right_pin_under(s, vp, pts["3V3"], MX - 70, bot + 44, ESP_X + ESP_W + 60, C["3v3"])
    to_left_pin(s, gp, pts["GNDL"], MX - 35, C["gnd"])
    return finish(s, bot,
                  ["KY-038 AO → D35 (GPIO35)", "KY-038 DO → D32 (GPIO32)", "KY-038 + → 3V3 (ห้าม 5 V)", "KY-038 G → GND"],
                  ["p2p=  12  DO=0  (เงียบ: ค่า p2p ต่ำ)",
                   "ตบมือ/พูดใกล้ๆ → p2p พุ่งขึ้น และแถบ #### ยาวขึ้น · ปรับ trimmer จน DO=1 เฉพาะตอนเสียงดัง"],
                  ["เลี้ยง 5 V จะทำให้ AO เกิน 3.3 V → ADC เสียได้"])


# ---------------------------------------------------------------- t05
def t05():
    used = {"D18": 1, "D33": 1, "VIN": 1, "GNDL": 1, "GNDR": 1}
    s, pts, ry, bot = base("t05 · Servo SG90 + ปุ่มหยุดฉุกเฉิน — บอร์ด Gateway",
                           "tests/t05_servo · SERVO_PIN = 18 (LEDC 50 Hz) · STOP_BTN = 33 (INPUT_PULLUP, FALLING)", used, "GATEWAY")
    # servo on right
    x, y = pts["D18"]
    sx, sw_ = 1200, 230
    s.module(sx, y - 70, sw_, 160, "Servo SG90", ["สาย 3 สี"], fill="#fdeeee", stroke="#b85450")
    s.mpin(sx, y, "ส้ม (สัญญาณ)", "left")
    sig(s, (x, y), (sx, y), C["s1"], "→ GPIO18")
    vp = (sx + sw_, y - 20); gp = (sx + sw_, y + 40)
    s.mpin(*vp, "แดง (+5V)", "right"); s.mpin(*gp, "น้ำตาล (GND)", "right")
    gx, vx, cx = sx + sw_ + 40, sx + sw_ + 130, sx + sw_ + 85
    yb = bot + 44
    s.line([gp, (gx, gp[1]), (gx, pts["GNDR"][1]), pts["GNDR"]], C["gnd"])
    s.line([vp, (vx, vp[1]), (vx, yb), (ESP_X - 60, yb), (ESP_X - 60, pts["VIN"][1]), pts["VIN"]], C["5v"])
    # cap between 5V (vx) and GND (gx)
    yc = y + 120
    s.dot(vx, yc, C["5v"]); s.line([(vx, yc), (cx, yc)], C["5v"])
    s.cap_v(cx, yc, yc + 50, "", C["5v"])
    s.line([(cx, yc + 50), (gx, yc + 50)], C["gnd"]); s.dot(gx, yc + 50, C["gnd"])
    s.text(vx + 14, yc + 30, "470–1000µF", 13, "start", 700, "#333")
    s.text(vx + 14, yc + 48, "(ขาสั้น/แถบ − ไป GND)", 11, "start", 400, "#555")
    # button on left
    x, y = pts["D33"]
    bx = 520
    s.line([(x, y), (bx + 50, y)], C["s5"])
    s.add(f'<circle cx="{bx+50}" cy="{y}" r="4" fill="#fff" stroke="#333" stroke-width="2"/>')
    s.add(f'<circle cx="{bx}" cy="{y}" r="4" fill="#fff" stroke="#333" stroke-width="2"/>')
    s.line([(bx - 4, y - 18), (bx + 54, y - 18)], "#333", sw=3)
    s.line([(bx + 25, y - 18), (bx + 25, y - 30)], "#333", sw=3)
    s.text(bx + 25, y - 40, "ปุ่มกด (push button)", 13, "middle", 700, "#333")
    s.wire_label(660, y, "→ GPIO33", C["s5"])
    s.line([(bx - 4, y), (bx - 60, y), (bx - 60, pts["GNDL"][1]), pts["GNDL"]], C["gnd"])
    return finish(s, bot,
                  ["Servo ส้ม → D18 (GPIO18)", "Servo แดง → VIN (5 V)", "Servo น้ำตาล → GND",
                   "C 470–1000µF คร่อม 5V–GND ใกล้เซอร์โว (ขั้ว + ไป 5V)", "ปุ่ม: ขาหนึ่ง → D33, อีกขา → GND (ไม่ต้องมีตัวต้านทาน ใช้ pull-up ภายใน)"],
                  ["-> 90 deg (ยกป้าย) / -> 0 deg (ลดป้าย) สลับทุก 2 วินาที",
                   "กดปุ่ม → E-STOP pressed. Servo released. (กด EN เพื่อเริ่มใหม่)"],
                  ["ถ้าบอร์ดรีเซ็ตตอนเซอร์โวหมุน = ไฟ USB ไม่พอ: ใช้พอร์ต/อะแดปเตอร์ ≥1 A หรือแยกไฟ 5 V ให้เซอร์โว (GND ร่วม)",
                   "sketch ใช้ ESP32 Arduino core 3.x (ledcAttach) — ถ้า compile error ให้อัปเดต board package"])


# ---------------------------------------------------------------- t06
def t06():
    used = {"D25": 1, "D26": 1, "VIN": 1, "GNDL": 1}
    s, pts, ry, bot = base("t06 · อัลตราโซนิก HC-SR04 — บอร์ด Node",
                           "tests/t06_ultrasonic · TRIG = 25, ECHO = 26 · ตอนทดสอบใช้ไฟ 5 V จาก VIN (USB) แทน MT3608", used, "NODE")
    left_box(s, 6.4, 9.0, "HC-SR04", ["อัลตราโซนิก (5 V)"], ry)
    tp = (MX + MW, ry(7)); ep = (MX + MW, ry(8))
    s.mpin(*tp, "Trig", "right"); s.mpin(*ep, "Echo", "right")
    sig(s, tp, pts["D25"], C["s1"], "Trig → GPIO25")
    y = ry(8)
    s.resistor_h(MX + MW, 560, y, "1kΩ", C["s2"])
    s.line([(560, y), pts["D26"]], C["s2"]); s.dot(600, y, C["s2"])
    s.wire_label(680, y, "→ GPIO26", C["s2"])
    gy = pts["GNDL"][1]
    s.resistor_v(600, y, y + 110, "2kΩ", C["gnd"], "right")
    s.line([(600, y + 110), (600, gy)], C["gnd"]); s.dot(600, gy, C["gnd"])
    vp = (MX, ry(7)); gp = (MX, ry(8))
    s.mpin(*vp, "VCC", "left"); s.mpin(*gp, "GND", "left")
    to_left_pin(s, vp, pts["VIN"], MX - 70, C["5v"])
    to_left_pin(s, gp, pts["GNDL"], MX - 35, C["gnd"])
    s.text(MX + MW / 2, ry(9.0) + 46, "Echo ออก 5 V → ต้องแบ่งแรงดันก่อนเข้า ESP32", 12, "middle", 700, "#b00020")
    return finish(s, bot,
                  ["HC-SR04 Trig → D25 (GPIO25)",
                   "HC-SR04 Echo → ตัวต้านทาน 1kΩ → D26 (GPIO26)  และจากจุด D26 ต่อ 2kΩ ลง GND",
                   "HC-SR04 VCC → VIN (5 V)", "HC-SR04 GND → GND"],
                  ["dist = 35.2 cm ทุกครั้งที่อ่าน · เอามือเข้าใกล้/ออกห่าง ค่าต้องเปลี่ยนตาม",
                   "no echo (timeout) = ไม่มีวัตถุในระยะ หรือสาย Echo/Trig ผิด"],
                  ["ถ้าไม่มี 2kΩ ใช้ 1kΩ 2 ตัวต่ออนุกรมแทนได้ · ถ้าเป็นรุ่น HC-SR04P (3.3 V) ต่อ VCC → 3V3 และ Echo ตรงเข้า GPIO26 ได้เลย"])


# ---------------------------------------------------------------- t07/t08
def t07():
    used = {"D19": 1, "D18": 1, "3V3": 1, "GNDR": 1}
    s, pts, ry, bot = base("t07 / t08 · Load cell + HX711 — บอร์ด Node",
                           "tests/t07_hx711_cal (หา CAL_FACTOR) และ tests/t08_hx711_weigh (ชั่งจริง) ใช้วงจรเดียวกัน · DT = 19, SCK = 18", used, "NODE")
    kx, kw = 1240, 200
    ky1, ky2 = ry(4.4), ry(7.4)
    s.module(kx, ky1, kw, ky2 - ky1, "HX711", None, fill="#eefbf1", stroke="#3c8d4f")
    for pin, name, col, lab in (("D19", "DT", C["s5"], "DT → GPIO19"), ("D18", "SCK", C["s6"], "SCK → GPIO18")):
        x, y = pts[pin]
        s.mpin(kx, y, name, "left"); sig(s, (x, y), (kx, y), col, lab)
    gpx, vpx = kx + 50, kx + 130
    s.mpin(gpx, ky2, "GND", "bottom"); s.mpin(vpx, ky2, "VCC", "bottom")
    s.line([(gpx, ky2), (gpx, pts["GNDR"][1]), pts["GNDR"]], C["gnd"])
    s.line([(vpx, ky2), (vpx, pts["3V3"][1]), pts["3V3"]], C["3v3"])
    lcx = 1530
    s.module(lcx, ky1 - 10, 140, ky2 - ky1 + 20, "Load cell", ["1 kg"], fill="#f5f5f5", stroke="#666")
    for k, (n, col, th) in enumerate([("E+", C["5v"], "แดง"), ("E−", C["gnd"], "ดำ"), ("A−", C["white"], "ขาว"), ("A+", C["s2"], "เขียว")]):
        yy = ky1 + 40 + k * 30
        s.mpin(kx + kw, yy, n, "right")
        s.line([(kx + kw, yy), (lcx, yy)], col)
        s.pad(lcx, yy)
        s.text(lcx + 10, yy + 5, th, 12, "start", 700, col if col != C["white"] else "#666")
    return finish(s, bot,
                  ["HX711 DT → D19 (GPIO19)", "HX711 SCK → D18 (GPIO18)", "HX711 VCC → 3V3 (ถ้ามีขา VDD แยก ต่อ 3V3 ด้วย)", "HX711 GND → GND",
                   "Load cell แดง/ดำ/ขาว/เขียว → E+ / E− / A− / A+ ของ HX711"],
                  ["t07: ยกของออก → รอ tare → วางของรู้น้ำหนัก พิมพ์กรัมแล้ว Enter → raw = ...  ->  CAL_FACTOR = ...  (ทำ 3 รอบ เฉลี่ย จดลง docs/calibration.md)",
                   "t08: ใส่ CAL_FACTOR ในโค้ด → weight =   250.0 g · ส่ง 't' เพื่อ tare ใหม่"],
                  ["HX711 not ready - เช็คสาย = DT/SCK/VCC ผิด · น้ำหนักติดลบ = สลับ A+ กับ A− · ยึด load cell ให้ปลายหนึ่งลอย (ตามลูกศรบนตัว)"])


# ---------------------------------------------------------------- t09
def t09():
    used = {"D22": 1, "D21": 1, "3V3": 1, "GNDR": 1}
    s, pts, ry, bot = base("t09 · INA219 วัดกระแส/แรงดัน — บอร์ด Node",
                           "tests/t09_ina219 · SDA = 21, SCL = 22 · I²C ที่อยู่ 0x40 · ใช้ตัวต้านทาน 100 Ω เป็นโหลดทดสอบ (ต่ออนุกรมผ่าน VIN+ / VIN−)", used, "NODE")
    kx, kw = 1240, 200
    ky1, ky2 = ry(1) - 70, ry(6)
    s.module(kx, ky1, kw, ky2 - ky1, "INA219", ["วัดกระแส/แรงดัน"], fill="#f3eefb", stroke="#7b5ea7")
    for pin, name, col, lab in (("D22", "SCL", C["s3"], "SCL → GPIO22"), ("D21", "SDA", C["s4"], "SDA → GPIO21")):
        x, y = pts[pin]
        s.mpin(kx, y, name, "left"); sig(s, (x, y), (kx, y), col, lab)
    gpx, vpx = kx + 50, kx + 130
    y3, yg = pts["3V3"][1], pts["GNDR"][1]
    s.mpin(gpx, ky2, "GND", "bottom"); s.mpin(vpx, ky2, "VCC", "bottom")
    s.line([(gpx, ky2), (gpx, yg), pts["GNDR"]], C["gnd"])
    s.line([(vpx, ky2), (vpx, y3), pts["3V3"]], C["3v3"])
    # VIN+ ← 3V3 (ต่อเข้าเส้น 3V3 เดียวกับ VCC) · VIN− → โหลด 100 Ω → GND
    pp, nn = (kx + kw, ry(2)), (kx + kw, ry(3))
    s.mpin(*pp, "VIN+", "right"); s.mpin(*nn, "VIN−", "right")
    xp, xl = kx + kw + 170, kx + kw + 120
    s.line([pp, (xp, pp[1]), (xp, y3), (vpx, y3)], C["3v3"]); s.dot(vpx, y3, C["3v3"])
    s.wire_label((kx + kw + xp) / 2, pp[1], "3V3 → VIN+", C["3v3"])
    s.line([nn, (xl, nn[1])], C["3v3"])
    s.wire_label((kx + kw + xl) / 2, nn[1], "VIN− → R", C["3v3"])
    s.resistor_v(xl, nn[1], nn[1] + 160, "100Ω", C["3v3"], "left")
    s.text(xl - 15, nn[1] + 100, "โหลดทดสอบ", 12, "end", 400, "#555")
    s.gnd(xl, nn[1] + 160)
    return finish(s, bot,
                  ["INA219 SDA → D21 (GPIO21)", "INA219 SCL → D22 (GPIO22)", "INA219 VCC → 3V3", "INA219 GND → GND",
                   "3V3 → INA219 VIN+ (ต่อเข้าจุด 3V3 เดียวกับ VCC ได้)",
                   "INA219 VIN− → ตัวต้านทาน 100Ω → GND (ต่ออนุกรม ให้กระแสไหลผ่านขา VIN+ → VIN−)"],
                  ["INA219 test (0x40) แล้วตามด้วย  bus = 3.290 V  shunt = 3.29 mV  I = 32.9 mA  P = 108.2 mW  ทุก 1 วินาที",
                   "ค่า I ต้องใกล้เคียง V ÷ R (≈33 mA) หรือเทียบกับมัลติมิเตอร์โหมด mA ที่ต่ออนุกรม · ถอดสาย VIN− ออก → I ≈ 0 mA"],
                  ["INA219 not found = สาย SDA/SCL/VCC หลวมหรือสลับ · ที่อยู่ต้องเป็น 0x40 (A0/A1 ไม่ต่อ) · ต้องติดตั้งไลบรารี Adafruit INA219",
                   "ห้ามต่อ VIN+ กับ VIN− ตรงกันเฉย ๆ หรือคร่อมขนานกับโหลด: shunt ต้องอยู่อนุกรมกับสายบวกของโหลด · ถ้ากระแสเกิน 400 mA เปลี่ยนเป็น setCalibration_32V_1A()",
                   "ตอนวัดโหนดจริง (E3): VIN+ ← TP4056 OUT+, VIN− → VBAT (ดู pin-table.md ข้อ 1) · GND ของ INA219 ต้องร่วมกับ ESP32"])


# ---------------------------------------------------------------- t10
def t10():
    used = {"D34": 1, "GNDL": 1}
    s, pts, ry, bot = base("t10: TP4056 + แบต 18650 + วัดแรงดันแบต (บอร์ด Node)",
                           "tests/t10_tp4056  (อ่าน VBAT ที่ GPIO34 ผ่านตัวแบ่ง 100k/100k)", used, "NODE")
    # แบต + TP4056 ฝั่งซ้าย
    s.module(40, 500, 150, 150, "แบต 18650", ["3.0–4.2 V", "เช็คขั้วก่อนใส่"], fill="#fdeeee", stroke="#b85450")
    s.mpin(190, 565, "B+", "right"); s.mpin(190, 611, "B−", "right")
    tx, tw = 300, 220
    s.module(tx, 470, tw, 200, "TP4056 (มีวงจรป้องกัน)",
             ["ช่อง USB บนโมดูล = เสียบชาร์จ", "LED แดง = กำลังชาร์จ", "LED น้ำเงิน/เขียว = เต็ม"],
             fill="#fdeeee", stroke="#b85450")
    s.mpin(tx, 565, "B+", "left"); s.mpin(tx, 611, "B−", "left")
    s.mpin(tx + tw, 565, "OUT+", "right"); s.mpin(tx + tw, 611, "OUT−", "right")
    s.line([(190, 565), (tx, 565)], C["5v"]); s.line([(190, 611), (tx, 611)], C["gnd"])
    s.text(tx + tw / 2, 700, "วัดมัลติมิเตอร์ที่ OUT+ เทียบ OUT−", 12, "middle", 700, "#7a2e00")
    # ตัวแบ่ง 100k/100k ที่ D34
    x34, y34 = pts["D34"]
    xg, yg = pts["GNDL"]
    nx, ytop = 660, 228
    s.line([(tx + tw, 565), (550, 565), (550, ytop), (nx, ytop)], C["vbat"])
    s.wire_label((550 + nx) / 2, ytop, "VBAT", C["vbat"])
    s.resistor_v(nx, ytop, y34, "100kΩ", C["vbat"], "left")
    s.line([(nx, y34), (x34, y34)], C["vbat"]); s.dot(nx, y34, C["vbat"])
    s.resistor_v(nx, y34, y34 + 120, "100kΩ", C["gnd"], "right")
    s.line([(nx, y34 + 120), (nx, yg)], C["gnd"]); s.dot(nx, yg, C["gnd"])
    # OUT− → ราง GND → ขา GND ของ ESP32
    s.line([(tx + tw, 611), (535, 611), (535, yg), (xg, yg)], C["gnd"])
    return finish(s, bot,
                  ["แบต 18650 ขั้ว + → TP4056 B+",
                   "แบต 18650 ขั้ว − → TP4056 B−",
                   "TP4056 OUT+ → ตัวต้านทาน 100kΩ ตัวบน → จุดกลาง",
                   "จุดกลาง → D34 (GPIO34)",
                   "จุดกลาง → ตัวต้านทาน 100kΩ ตัวล่าง → GND",
                   "TP4056 OUT− → GND ของ ESP32"],
                  ["t10 TP4056 + battery sense (GPIO34) แล้วตามด้วยบรรทัดแบบนี้ทุก 1 วินาที",
                   "pin = 1910 mV  VBAT = 3.82 V  ~60 %  d30s = +0.000 V  [ok]",
                   "VBAT ต้องต่างจากมัลติมิเตอร์ (วัด OUT+ เทียบ OUT−) ไม่เกิน 0.05 V",
                   "ถ้าต่างคงที่ ให้ตั้ง VBAT_GAIN = ค่ามัลติมิเตอร์ ÷ ค่าที่อ่านได้",
                   "เสียบสายชาร์จที่ TP4056 → LED แดงติด และ d30s เป็นบวก (แรงดันค่อยๆ ขึ้น)",
                   "ชาร์จเต็ม → VBAT ≈ 4.20 V และ LED เปลี่ยนเป็นน้ำเงินหรือเขียว"],
                  ["ใช้ TP4056 รุ่น 6 ขา (มีวงจรป้องกัน) และต่อโหลดที่ OUT+ กับ OUT− เท่านั้น ห้ามต่อโหลดที่ B−",
                   "ใส่แบตกลับขั้ว TP4056 พังทันที ให้เช็คขั้วด้วยมัลติมิเตอร์ก่อน",
                   "เทสนี้ ESP32 ใช้ไฟ USB อย่างเดียว ห้ามต่อ VBAT เข้าขา 3V3 หรือ VIN",
                   "VBAT อ่านได้ 0 = สาย OUT+ หรือตัวต้านทานตัวบนหลุด",
                   "VBAT สูงเกิน 4.25 V = ตัวต้านทานผิดค่า (ต้องเป็น 100k ทั้งสองตัว)",
                   "ไม่มี C กรองไฟ ค่าอาจแกว่งได้เล็กน้อย (โค้ดเฉลี่ย 16 ครั้งให้แล้ว)"])


# ---------------------------------------------------------------- t11
def t11():
    used = {"3V3": 1, "GNDR": 1, "D34": 1, "GNDL": 1}
    s, pts, ry, bot = base("t11: LDO 3.3 V จ่ายไฟให้ ESP32 จากแบต (บอร์ด Node)",
                           "tests/t11_ldo_3v3  (ESP32 กินไฟผ่านขา 3V3 แล้วจดผลลง flash ให้เสียบ USB อ่านทีหลัง)",
                           used, "NODE",
                           usb_text="ตอนรันด้วยไฟจาก LDO ห้ามเสียบ USB (เสียบเฉพาะตอนอัปโหลดและตอนอ่าน log)",
                           usb_color="#b00020")
    # แบต → TP4056 (ขวาบน)
    s.module(1130, 170, 150, 130, "แบต 18650", ["3.0–4.2 V"], fill="#fdeeee", stroke="#b85450")
    s.mpin(1280, 230, "B+", "right"); s.mpin(1280, 265, "B−", "right")
    tx, tw = 1370, 250
    s.module(tx, 150, tw, 170, "TP4056 (มีวงจรป้องกัน)", ["ต่อแบบเดียวกับ t10"], fill="#fdeeee", stroke="#b85450")
    s.mpin(tx, 230, "B+", "left"); s.mpin(tx, 265, "B−", "left")
    s.mpin(tx + tw, 230, "OUT+", "right"); s.mpin(tx + tw, 265, "OUT−", "right")
    s.line([(1280, 230), (tx, 230)], C["5v"]); s.line([(1280, 265), (tx, 265)], C["gnd"])
    s.line([(tx + tw, 265), (tx + tw + 20, 265)], C["gnd"]); s.gnd(tx + tw + 20, 265)
    # OUT+ (VBAT) → LDO VIN
    lx, lw, ly = 1340, 260, 420
    vx = tx + tw + 45
    s.line([(tx + tw, 230), (vx, 230), (vx, ly + 50), (lx + lw, ly + 50)], C["vbat"])
    s.dot(vx, 370, C["vbat"]); s.flag(vx, 370, "VBAT", C["vbat"], "left")
    s.module(lx, ly, lw, 110, "LDO 3.3 V", ["AP2112K หรือ ME6211", "ห้ามใช้ AMS1117"], fill="#fff2e0", stroke="#d98200")
    s.mpin(lx + lw, ly + 50, "VIN", "right"); s.mpin(lx, ly + 50, "VOUT", "left")
    s.mpin(lx + lw / 2, ly + 110, "GND", "bottom"); s.gnd(lx + lw / 2, ly + 110)
    # VOUT → C → ขา 3V3
    x3, y3 = pts["3V3"]
    rx = 1180
    s.line([(lx, ly + 50), (rx, ly + 50), (rx, y3), (x3, y3)], C["3v3"])
    s.wire_label((x3 + rx) / 2, y3, "→ 3V3", C["3v3"])
    s.text(1200, 770, "วัดขา 3V3 ขณะรัน ต้องไม่ต่ำกว่า 3.2 V", 12, "start", 700, "#7a4a00")
    xg, yg = pts["GNDR"]
    s.line([(xg, yg), (xg + 48, yg)], C["gnd"]); s.gnd(xg + 48, yg, down=False)
    # ตัวแบ่งวัดแบต (ค้างไว้จาก t10 ไม่บังคับ)
    x34, y34 = pts["D34"]
    nx = 660
    s.line([(x34, y34), (nx, y34)], C["vbat"]); s.dot(nx, y34, C["vbat"])
    s.resistor_v(nx, y34 - 110, y34, "100kΩ", C["vbat"], "left")
    s.flag(nx, y34 - 110, "VBAT", C["vbat"], "up")
    s.resistor_v(nx, y34, y34 + 110, "100kΩ", C["gnd"], "right"); s.gnd(nx, y34 + 110)
    s.text(610, y34 + 180, "ตัวแบ่งวัดแบตจาก t10 (ไม่บังคับ)", 12, "middle", 700, C["vbat"])
    s.text(610, y34 + 198, "ถ้าต่อไว้ log จะมี VBAT ต่ำสุดด้วย", 11, "middle", 400, "#555")
    xl, yl = pts["GNDL"]
    s.line([(xl, yl), (xl - 30, yl)], C["gnd"]); s.gnd(xl - 30, yl)
    xv, yv = pts["VIN"]
    s.text(xv - 8, yv + 5, "ไม่ต่อ", 12, "end", 400, "#999", italic=True)
    return finish(s, bot,
                  ["แบต 18650 → TP4056 B+ และ B− (เหมือน t10)",
                   "TP4056 OUT+ (VBAT) → LDO VIN",
                   "LDO VOUT → ขา 3V3 ของ ESP32",
                   "TP4056 OUT− กับ LDO GND กับ GND ของ ESP32 ต่อถึงกันทั้งหมด (สัญลักษณ์กราวด์ทุกอันคือจุดเดียวกัน)",
                   "ขา VIN ของ ESP32 ไม่ต่อ",
                   "ตัวแบ่ง 100k/100k ที่ D34 จาก t10 ค้างไว้ได้ (ไม่บังคับ)"],
                  ["ขั้น 1: ยังไม่ต่อ VOUT เข้า ESP32 วัด LDO VOUT เทียบ GND ด้วยมัลติมิเตอร์ ต้องได้ 3.25–3.35 V",
                   "ขั้น 2: เสียบ USB อัปโหลด t11 เปิด Serial Monitor 115200 ส่ง c เพื่อล้าง log แล้วถอด USB",
                   "ขั้น 3: ต่อ VOUT เข้า 3V3 → LED บนบอร์ดกะพริบช้า (1 ครั้งต่อวินาที) ปล่อยไว้ 3–5 นาที",
                   "ระหว่างรอ วัดขา 3V3 ด้วยมัลติมิเตอร์ ต้องไม่ต่ำกว่า 3.2 V",
                   "ขั้น 4: ถอดสาย VOUT ออกจาก 3V3 ก่อน แล้วค่อยเสียบ USB → Serial พิมพ์ผลของรอบที่รันจาก LDO",
                   "ตัวอย่างผล: รอบก่อนรันได้ 240 s",
                   "ตัวอย่างผล: brownout สะสม = 0",
                   "ตัวอย่างผล: VBAT ต่ำสุดรอบก่อน = 3850 mV",
                   "ผ่าน = brownout เป็น 0 และเวลาที่รันได้ตรงกับเวลาที่ปล่อยไว้"],
                  ["ห้ามเสียบ USB ขณะ LDO จ่ายเข้า 3V3 (ไฟสองแหล่งชนกันบนราง 3.3 V)",
                   "LED กะพริบเร็ว (5 ครั้งต่อวินาที) = เคยเกิด brownout ตั้งแต่ล้าง log",
                   "ถ้า brownout: เพิ่ม C 100µF คร่อม 3V3 กับ GND ชิดขา 3V3 (ขาบวกไป 3V3)",
                   "ถ้า brownout: ใช้สายไฟให้สั้นที่สุด",
                   "ถ้า brownout: LDO ต้องจ่ายได้อย่างน้อย 500 mA และแบตต้องไม่ใกล้หมด",
                   "ห้ามใช้ AMS1117 เพราะต้องการไฟเข้าอย่างน้อยประมาณ 4.3 V (แบตให้ได้แค่ 3.0–4.2 V)",
                   "ถ้าเป็นชิป AP2112K เปล่า ขา EN ต้องต่อเข้า VIN ไม่อย่างนั้น LDO ไม่จ่ายไฟ"],
                  expect_title="ขั้นตอนทดสอบ")


# ---------------------------------------------------------------- t13
def t13():
    used = {"D34": 1, "GNDL": 1}
    s, pts, ry, bot = base("t13: ถ่าน 18650 1 ก้อน + วัดแรงดันแบต (บอร์ด Node)",
                           "tests/t13_battery_18650  (อ่าน VBAT ที่ GPIO34 ผ่านตัวแบ่ง 100k/100k · ไม่มี TP4056)", used, "NODE")
    # แบต 18650 ฝั่งซ้าย ต่อตรงเข้าตัวแบ่ง
    s.module(40, 500, 190, 150, "แบต 18650 (1 ก้อน)", ["3.0–4.2 V", "เช็คขั้วก่อนต่อ"], fill="#fdeeee", stroke="#b85450")
    s.mpin(230, 565, "+", "right"); s.mpin(230, 611, "−", "right")
    s.text(135, 690, "วัดมัลติมิเตอร์ที่ขั้ว + เทียบ −", 12, "middle", 700, "#7a2e00")
    x34, y34 = pts["D34"]
    xg, yg = pts["GNDL"]
    nx, ytop = 660, 228
    s.line([(230, 565), (550, 565), (550, ytop), (nx, ytop)], C["vbat"])
    s.wire_label((550 + nx) / 2, ytop, "VBAT", C["vbat"])
    s.resistor_v(nx, ytop, y34, "100kΩ", C["vbat"], "left")
    s.line([(nx, y34), (x34, y34)], C["vbat"]); s.dot(nx, y34, C["vbat"])
    s.resistor_v(nx, y34, y34 + 120, "100kΩ", C["gnd"], "right")
    s.line([(nx, y34 + 120), (nx, yg)], C["gnd"]); s.dot(nx, yg, C["gnd"])
    s.line([(230, 611), (535, 611), (535, yg), (xg, yg)], C["gnd"])
    return finish(s, bot,
                  ["แบต 18650 ขั้ว + → ตัวต้านทาน 100kΩ ตัวบน → จุดกลาง",
                   "จุดกลาง → D34 (GPIO34)",
                   "จุดกลาง → ตัวต้านทาน 100kΩ ตัวล่าง → GND",
                   "แบต 18650 ขั้ว − → GND ของ ESP32"],
                  ["t13 18650 single cell (GPIO34) แล้วตามด้วยบรรทัดแบบนี้ทุก 1 วินาที",
                   "pin = 1910 mV  VBAT = 3.820 V  ~60 %  [ok]",
                   "ทุก 10 วินาทีมีสรุป: --- 10 s: min / max / avg และ แกว่ง (นิ่งดี = ต่ำกว่า 30 mV)",
                   "วัดแบตด้วยมัลติมิเตอร์ แล้วส่ง g<ค่าที่วัดได้> เช่น g3.852 → โค้ดตั้ง VBAT_GAIN ให้ (ต่างไม่เกิน 0.05 V ก็ผ่าน)",
                   "ค่า VBAT_GAIN ที่ได้ ให้ใส่ใน t10 / t14 ด้วย · ส่ง r = ล้างสถิติ 10 วินาที"],
                  ["ใส่แบตกลับขั้ว: ตัวแบ่งไม่พังทันที แต่ ESP32 เห็นแรงดันติดลบ (VBAT ≈ 0) ให้เช็คขั้วด้วยมัลติมิเตอร์ก่อนต่อ",
                   "เทสนี้ ESP32 ใช้ไฟ USB อย่างเดียว ห้ามต่อแบตเข้าขา 3V3 หรือ VIN โดยตรง",
                   "แบตเต็มต้องเห็น ≈ 4.20 V · ถ้าต่ำกว่า 3.0 V ห้ามใช้ต่อ ให้ชาร์จก่อน · ต่ำกว่า 2.8 V = ทิ้งหรือเปลี่ยนก้อน",
                   "VBAT อ่านได้ 0 = สายแบต + หรือตัวต้านทานตัวบนหลุด · สูงเกิน 4.25 V = ตัวต้านทานผิดค่า (ต้อง 100k ทั้งสองตัว)",
                   "ตัวแบ่งกินกระแสต่อเนื่อง ≈ 21 µA (4.2 V ÷ 200 kΩ) ถ้าเก็บแบตไว้นาน ให้ถอดสายตัวแบ่งออก",
                   "ไม่มี C กรองไฟ ค่าอาจแกว่งเล็กน้อย (โค้ดเฉลี่ย 16 ครั้งให้แล้ว)"])


# ---------------------------------------------------------------- t14
def t14():
    used = {"D34": 1, "D33": 1, "GNDL": 1}
    s, pts, ry, bot = base("t14: MT3608 boost 5 V จากแบต 18650 (บอร์ด Node)",
                           "tests/t14_mt3608  (ESP32 ใช้ไฟ USB · อ่าน VIN ที่ GPIO34 และ VOUT ที่ GPIO33 ผ่านตัวแบ่ง 100k/100k)",
                           used, "NODE")
    # แบต → MT3608
    s.module(40, 500, 150, 150, "แบต 18650", ["3.0–4.2 V"], fill="#fdeeee", stroke="#b85450")
    s.mpin(190, 565, "+", "right"); s.mpin(190, 611, "−", "right")
    mx, mw = 300, 240
    s.module(mx, 470, mw, 200, "MT3608 boost",
             ["ปรับ trimmer เป็น 5.0 V", "ก่อนต่อสายเข้า D33 !", "(ค่าเดิมจากโรงงานอาจสูงมาก)"],
             fill="#fff2e0", stroke="#d98200")
    s.mpin(mx, 565, "VIN+", "left"); s.mpin(mx, 611, "VIN−", "left")
    s.mpin(mx + mw, 565, "VOUT+", "right"); s.mpin(mx + mw, 611, "VOUT−", "right")
    s.line([(190, 565), (mx, 565)], C["vbat"]); s.line([(190, 611), (mx, 611)], C["gnd"])
    s.dot(245, 565, C["vbat"]); s.flag(245, 565, "VBAT", C["vbat"], "up")
    s.dot(245, 611, C["gnd"]); s.gnd(245, 611)
    s.line([(mx + mw, 565), (mx + mw + 40, 565)], C["5v"])
    s.dot(mx + mw + 40, 565, C["5v"]); s.flag(mx + mw + 40, 565, "+5V", C["5v"], "up")
    s.line([(mx + mw, 611), (mx + mw + 40, 611)], C["gnd"]); s.gnd(mx + mw + 40, 611)
    s.text(mx + mw / 2, 700, "วัด VOUT+ เทียบ VOUT− ด้วยมัลติมิเตอร์", 12, "middle", 700, "#7a2e00")
    # ตัวแบ่ง VIN → D34
    x34, y34 = pts["D34"]
    n1 = 600
    s.line([(x34, y34), (n1, y34)], C["vbat"]); s.dot(n1, y34, C["vbat"])
    s.resistor_v(n1, y34 - 110, y34, "100kΩ", C["vbat"], "left")
    s.flag(n1, y34 - 110, "VBAT", C["vbat"], "up")
    s.resistor_v(n1, y34, y34 + 110, "100kΩ", C["gnd"], "right"); s.gnd(n1, y34 + 110)
    s.text(n1, y34 - 158, "VIN → D34", 12, "middle", 700, C["vbat"])
    # ตัวแบ่ง VOUT → D33
    x33, y33 = pts["D33"]
    n2 = 685
    s.line([(x33, y33), (n2, y33)], C["5v"]); s.dot(n2, y33, C["5v"])
    s.resistor_v(n2, y33 - 70, y33, "100kΩ", C["5v"], "left")
    s.flag(n2, y33 - 70, "+5V", C["5v"], "up")
    s.resistor_v(n2, y33, y33 + 110, "100kΩ", C["gnd"], "right"); s.gnd(n2, y33 + 110)
    s.text(n2 + 30, y33 + 165, "VOUT → D33 (≈2.5 V ที่ขา)", 12, "end", 700, C["5v"])
    # GND ของ ESP32
    xl, yl = pts["GNDL"]
    s.line([(xl, yl), (xl - 30, yl)], C["gnd"]); s.gnd(xl - 30, yl)
    return finish(s, bot,
                  ["แบต 18650 + → MT3608 VIN+ · แบต − → MT3608 VIN−",
                   "ยังไม่ต่อสายเข้า D33: ตั้ง trimmer จนมัลติมิเตอร์ที่ VOUT+ ได้ 5.0 V",
                   "VIN+ (VBAT) → ตัวต้านทาน 100kΩ ตัวบน → จุดกลาง → D34 · จุดกลาง → 100kΩ ตัวล่าง → GND",
                   "VOUT+ (+5V) → ตัวต้านทาน 100kΩ ตัวบน → จุดกลาง → D33 · จุดกลาง → 100kΩ ตัวล่าง → GND",
                   "แบต − กับ MT3608 VIN−/VOUT− กับ GND ของ ESP32 ต่อถึงกันทั้งหมด (สัญลักษณ์กราวด์ทุกอันคือจุดเดียวกัน)"],
                  ["ขั้น 1: ยังไม่เสียบ USB และยังไม่ต่อ D33 ต่อแบตเข้า MT3608 วัด VOUT ด้วยมัลติมิเตอร์ ปรับ trimmer ให้ได้ 5.0 V (ยอมรับ 4.9–5.1 V)",
                   "ขั้น 2: ต่อสายตัวแบ่งเข้า D33 และ D34 เสียบ USB เปิด Serial Monitor 115200",
                   "t14 MT3608 boost (VIN = GPIO34, VOUT = GPIO33) แล้วตามด้วยบรรทัดแบบนี้ทุก 0.5 วินาที",
                   "VIN = 3.85 V  VOUT = 5.012 V  สวิง = 24 mV  [ok]",
                   "VOUT ต้องต่างจากมัลติมิเตอร์ไม่เกิน 0.1 V ถ้าต่างคงที่ ให้ตั้ง VOUT_GAIN = ค่ามัลติมิเตอร์ ÷ ค่าที่อ่านได้",
                   "ขั้น 3 (ทดสอบโหลด): ส่ง b จำค่าไม่มีโหลด แล้วต่อ R 100Ω คร่อม +5V กับ GND (≈50 mA) · ตกจาก baseline ต้องไม่เกิน 0.1 V",
                   "ขั้น 4: เปลี่ยนแบตเป็นก้อนที่แรงดันต่ำลง (3.3–4.2 V) · VOUT ต้องยังได้ 5.0 V ± 0.1 V",
                   "ผ่าน = VOUT 4.9–5.1 V ตลอดช่วงแบต 3.3–4.2 V และตกเมื่อมีโหลดไม่เกิน 0.1 V"],
                  ["ห้ามต่อสายตัวแบ่ง D33 ก่อนปรับ trimmer: MT3608 ที่ยังไม่ปรับอาจออกสูงถึง ~28 V ขา ESP32 พังทันที",
                   "ตัวแบ่ง ÷2 ทำให้ขา D33 ปลอดภัยถึง VOUT ≈ 6.6 V · ถ้าอ่านได้เกิน 6 V โค้ดจะเตือนว่า 'อันตราย' ให้ถอดสายทันที",
                   "ห้ามต่อ VOUT เข้า 3V3 หรือ VIN ของ ESP32 · +5 V ใช้กับ HC-SR04 VCC เท่านั้น",
                   "สวิง (mV) เป็นค่าหยาบจาก ADC อ่านริปเปิลความถี่สวิตช์ไม่ได้ ถ้าต้องการวัดจริงใช้ออสซิลโลสโคป",
                   "VOUT ไม่สูงกว่า VIN = บูสต์ไม่ทำงาน หรือ trimmer ถูกหมุนต่ำสุด · ใส่แบตกลับขั้ว MT3608 พังได้ เช็คขั้วก่อนต่อ",
                   "ถ้า VOUT ตกมากตอนมีโหลด: ใช้สายสั้นและหนา · ตรวจว่าแบตไม่ใกล้หมด · เพิ่ม C 100µF ที่ VOUT"],
                  expect_title="ขั้นตอนทดสอบ")


if __name__ == "__main__":
    out = sys.argv[1]
    for name, fn in [("t01-blink", t01), ("t02-dht11", t02), ("t03-pir", t03), ("t04-sound", t04),
                     ("t05-servo", t05), ("t06-ultrasonic", t06), ("t07-t08-hx711", t07), ("t09-ina219", t09),
                     ("t10-tp4056", t10), ("t11-ldo-3v3", t11),
                     ("t13-battery-18650", t13), ("t14-mt3608", t14)]:
        fn().save(f"{out}/{name}.svg")
    print("ok")
