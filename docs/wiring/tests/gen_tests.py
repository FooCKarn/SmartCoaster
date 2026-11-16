#!/usr/bin/env python3
"""Per-sensor test wiring diagrams (tests/t01–t09). Uses helpers from ../gen.py style.

Run: python3 gen_tests.py <out_dir>   -> writes t0X-*.svg
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from gen import SVG, C, P, ESP_X, ESP_W, esp32, notes_box  # noqa: E402

W = 1700
MX, MW = 230, 250          # left module column
TOP = 120


def base(title, sub, used, board):
    s = SVG(W, 1300)
    s.text(40, 46, title, 26, "start", 700, "#1b2a41")
    s.text(40, 72, sub, 14, "start", 400, "#555")
    pts, y0, bot = esp32(s, TOP, used, board)
    s.text(ESP_X + ESP_W / 2, bot + 22, "เสียบ USB เข้าคอมพิวเตอร์ (จ่ายไฟ + Serial 115200)", 13, "middle", 700, "#1b2a41")
    return s, pts, (lambda i: y0 + i * P), bot


def finish(s, bot, wires, expect, extra=None):
    lines = ["▸ ต่อสาย"] + [f"   {i+1}) {w}" for i, w in enumerate(wires)]
    lines += ["▸ ผลที่ควรเห็นใน Serial Monitor (115200)"] + [f"   • {e}" for e in expect]
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


if __name__ == "__main__":
    out = sys.argv[1]
    for name, fn in [("t01-blink", t01), ("t02-dht11", t02), ("t03-pir", t03), ("t04-sound", t04),
                     ("t05-servo", t05), ("t06-ultrasonic", t06), ("t07-t08-hx711", t07), ("t09-ina219", t09)]:
        fn().save(f"{out}/{name}.svg")
    print("ok")
