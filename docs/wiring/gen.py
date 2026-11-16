#!/usr/bin/env python3
"""Generate pin-level wiring diagrams (SVG) for hydration coaster node + gateway."""
import html

FONT = "'Noto Sans Thai','Sarabun','Leelawadee UI','Tahoma',sans-serif"
C = {
    "5v": "#d62728", "3v3": "#f28e00", "gnd": "#222222", "vbat": "#8c564b",
    "s1": "#1f77b4", "s2": "#2ca02c", "s3": "#9467bd", "s4": "#17becf",
    "s5": "#e377c2", "s6": "#0b6e4f", "white": "#9a9a9a",
}
P = 46            # pin pitch on ESP32
ESP_X, ESP_W = 760, 300
LEFT_PINS = ["EN", "VP", "VN", "D34", "D35", "D32", "D33", "D25", "D26", "D27", "D14", "D12", "D13", "GND", "VIN"]
RIGHT_PINS = ["D23", "D22", "TX0", "RX0", "D21", "D19", "D18", "D5", "TX2", "RX2", "D4", "D2", "D15", "GND", "3V3"]
GPIO = {"VP": "GPIO36", "VN": "GPIO39", "TX0": "GPIO1", "RX0": "GPIO3", "TX2": "GPIO17", "RX2": "GPIO16"}


def esc(s):
    return html.escape(str(s))


class SVG:
    def __init__(self, w, h):
        self.w, self.h, self.o = w, h, []

    def add(self, s):
        self.o.append(s)

    def text(self, x, y, s, size=15, anchor="start", weight=400, fill="#222", italic=False):
        st = ' font-style="italic"' if italic else ""
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}"{st}>{esc(s)}</text>')

    def rect(self, x, y, w, h, fill="#fff", stroke="#333", rx=8, sw=2, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def line(self, pts, color, sw=3, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        p = " ".join(f"{x},{y}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{d}/>')

    def dot(self, x, y, color="#222", r=5):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

    def pad(self, x, y):
        self.add(f'<rect x="{x-5}" y="{y-5}" width="10" height="10" fill="#c9a227" stroke="#6b5600" stroke-width="1"/>')

    # ---- symbols ----
    def gnd(self, x, y, down=True):
        """ground symbol with stub starting at (x,y)."""
        s = 1 if down else -1
        self.line([(x, y), (x, y + s * 16)], C["gnd"])
        for i, hw in enumerate([13, 8, 3]):
            yy = y + s * (16 + i * 5)
            self.line([(x - hw, yy), (x + hw, yy)], C["gnd"], sw=2.5)

    def flag(self, x, y, label, color, direction="left"):
        """power / net flag. (x,y) is wire end; flag extends in direction."""
        w = max(46, 9 * len(label) + 18)
        h = 26
        if direction == "left":
            self.line([(x, y), (x - 14, y)], color)
            bx = x - 14 - w
            self.add(f'<path d="M{x-14},{y} L{x-24},{y-h/2} L{bx},{y-h/2} L{bx},{y+h/2} L{x-24},{y+h/2} Z" fill="{color}"/>')
            self.text(bx + (w - 10) / 2, y + 5, label, 13, "middle", 700, "#fff")
        elif direction == "right":
            self.line([(x, y), (x + 14, y)], color)
            bx = x + 14 + w
            self.add(f'<path d="M{x+14},{y} L{x+24},{y-h/2} L{bx},{y-h/2} L{bx},{y+h/2} L{x+24},{y+h/2} Z" fill="{color}"/>')
            self.text(x + 24 + (w - 10) / 2, y + 5, label, 13, "middle", 700, "#fff")
        elif direction == "up":
            self.line([(x, y), (x, y - 14)], color)
            self.rect(x - w / 2, y - 14 - h, w, h, fill=color, stroke=color, rx=5, sw=1)
            self.text(x, y - 14 - 8, label, 13, "middle", 700, "#fff")
        elif direction == "down":
            self.line([(x, y), (x, y + 14)], color)
            self.rect(x - w / 2, y + 14, w, h, fill=color, stroke=color, rx=5, sw=1)
            self.text(x, y + 14 + 18, label, 13, "middle", 700, "#fff")

    def resistor_h(self, x1, x2, y, label, color):
        """horizontal resistor between x1 and x2 (zigzag body in middle)."""
        mid = (x1 + x2) / 2
        bw = 44
        self.line([(x1, y), (mid - bw / 2, y)], color)
        self.line([(mid + bw / 2, y), (x2, y)], color)
        self.rect(mid - bw / 2, y - 9, bw, 18, fill="#f4e7c5", stroke="#444", rx=3, sw=1.6)
        self.text(mid, y - 15, label, 13, "middle", 700, "#333")

    def resistor_v(self, x, y1, y2, label, color, label_side="right"):
        mid = (y1 + y2) / 2
        bh = 40
        self.line([(x, y1), (x, mid - bh / 2)], color)
        self.line([(x, mid + bh / 2), (x, y2)], color)
        self.rect(x - 9, mid - bh / 2, 18, bh, fill="#f4e7c5", stroke="#444", rx=3, sw=1.6)
        if label_side == "right":
            self.text(x + 15, mid + 5, label, 13, "start", 700, "#333")
        else:
            self.text(x - 15, mid + 5, label, 13, "end", 700, "#333")

    def cap_v(self, x, y1, y2, label, color, polar=True):
        mid = (y1 + y2) / 2
        self.line([(x, y1), (x, mid - 5)], color)
        self.line([(x, mid + 5), (x, y2)], C["gnd"])
        self.line([(x - 14, mid - 5), (x + 14, mid - 5)], "#333", sw=3)
        self.add(f'<path d="M{x-14},{mid+9} Q{x},{mid+1} {x+14},{mid+9}" fill="none" stroke="#333" stroke-width="3"/>')
        if polar:
            self.text(x - 20, mid - 8, "+", 14, "middle", 700, "#333")
        self.text(x + 20, mid + 5, label, 13, "start", 700, "#333")

    def module(self, x, y, w, h, title, sub=None, fill="#eef4fb", stroke="#4a6fa5", dash=None):
        self.rect(x, y, w, h, fill=fill, stroke=stroke, dash=dash)
        self.text(x + w / 2, y + 22, title, 15, "middle", 700, "#1b2a41")
        if sub:
            for i, s in enumerate(sub if isinstance(sub, list) else [sub]):
                self.text(x + w / 2, y + 40 + i * 16, s, 12, "middle", 400, "#4b5563")

    def mpin(self, x, y, name, side):
        """module pin: pad on box edge, label inside box. side = edge of box the pin sits on."""
        self.pad(x, y)
        if side == "right":
            self.text(x - 10, y + 5, name, 13, "end", 600, "#333")
        elif side == "left":
            self.text(x + 10, y + 5, name, 13, "start", 600, "#333")
        elif side == "top":
            self.text(x, y + 22, name, 13, "middle", 600, "#333")
        elif side == "bottom":
            self.text(x, y - 12, name, 13, "middle", 600, "#333")

    def wire_label(self, x, y, s, color):
        w = 8.2 * len(s) + 14
        self.rect(x - w / 2, y - 25, w, 19, fill="#ffffff", stroke=color, rx=4, sw=1.2)
        self.text(x, y - 11, s, 12, "middle", 700, color)

    def save(self, path, bg="#ffffff"):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}">'
                f'<rect width="100%" height="100%" fill="{bg}"/>')
        with open(path, "w", encoding="utf-8") as f:
            f.write(head + "\n".join(self.o) + "</svg>")


def esp32(svg, top, used, title):
    """draw DOIT ESP32 DevKit V1 30-pin. used: {pin: (label, color)}. returns pin endpoint coords."""
    y0 = top + 100
    h = 100 + 14 * P + 60
    svg.rect(ESP_X, top, ESP_W, h, fill="#20303f", stroke="#0d1620", rx=10, sw=2)
    # antenna + module can
    svg.rect(ESP_X + 60, top + 10, ESP_W - 120, 30, fill="#38506a", stroke="#5a7896", rx=3, sw=1)
    svg.text(ESP_X + ESP_W / 2, top + 31, "ANTENNA (ด้านบน)", 11, "middle", 600, "#c9d6e3")
    svg.text(ESP_X + ESP_W / 2, top + 64, title, 16, "middle", 700, "#ffffff")
    svg.text(ESP_X + ESP_W / 2, top + 84, "ESP32 DevKit V1 (DOIT 30 ขา)", 12, "middle", 400, "#c9d6e3")
    svg.rect(ESP_X + ESP_W / 2 - 34, top + h - 26, 68, 26, fill="#9aa5b1", stroke="#555", rx=3, sw=1)
    svg.text(ESP_X + ESP_W / 2, top + h - 8, "USB", 12, "middle", 700, "#222")
    pts = {}
    for side, pins in (("L", LEFT_PINS), ("R", RIGHT_PINS)):
        for i, p in enumerate(pins):
            y = y0 + i * P
            key = p if p not in ("GND",) else f"GND{side}"
            u = used.get(key) or used.get(p)
            col = "#ffd166" if u else "#7f8c99"
            wt = 700 if u else 400
            gp = GPIO.get(p, ("GPIO" + p[1:]) if p.startswith("D") else "")
            lab = p + (f" ({gp})" if gp and gp != "GPIO" + p[1:] else "")
            if p.startswith("D"):
                lab = f"{p}  ·  GPIO{p[1:]}"
            if side == "L":
                svg.line([(ESP_X - 22, y), (ESP_X, y)], "#c9a227", sw=4)
                svg.text(ESP_X + 10, y + 5, lab, 13, "start", wt, col)
                pts[key] = (ESP_X - 22, y)
            else:
                svg.line([(ESP_X + ESP_W, y), (ESP_X + ESP_W + 22, y)], "#c9a227", sw=4)
                svg.text(ESP_X + ESP_W - 10, y + 5, lab, 13, "end", wt, col)
                pts[key] = (ESP_X + ESP_W + 22, y)
    return pts, y0, top + h


def legend(svg, x, y, items):
    svg.text(x, y, "สีสาย", 14, "start", 700)
    for i, (lab, col, dash) in enumerate(items):
        yy = y + 22 + i * 22
        svg.line([(x, yy - 5), (x + 40, yy - 5)], col, sw=4, dash=dash)
        svg.text(x + 50, yy, lab, 13)


def notes_box(svg, x, y, w, title, lines, fill="#fff8e6", stroke="#d9a400"):
    lh = 21
    h = 40 + lh * len(lines)
    svg.rect(x, y, w, h, fill=fill, stroke=stroke, rx=8, sw=1.5)
    svg.text(x + 16, y + 26, title, 15, "start", 700, "#5c4400")
    for i, l in enumerate(lines):
        svg.text(x + 16, y + 50 + i * lh, l, 13, "start", 400, "#333")
    return y + h


# =====================================================================
# NODE
# =====================================================================
def node():
    s = SVG(1800, 1540)
    s.text(40, 46, "วงจรโหนด (แผ่นรองแก้ว) — ต่อสายระดับขา", 26, "start", 700, "#1b2a41")
    s.text(40, 72, "Hydration coaster · Sensor node · ใช้แบต 18650 + deep sleep · ส่งข้อมูลผ่าน ESP-NOW ไปเกตเวย์", 14, "start", 400, "#555")

    # ---------- power chain ----------
    py = 140
    s.text(40, py - 22, "① ภาคจ่ายไฟ (ต่อเรียงจากซ้ายไปขวา)", 16, "start", 700, "#7a2e00")
    # battery
    s.module(40, py, 150, 120, "แบต 18650", ["3.0–4.2 V", "ในรางถ่าน"], fill="#fdeeee", stroke="#b85450")
    s.mpin(190, py + 80, "B+", "right"); s.mpin(190, py + 104, "B−", "right")
    # TP4056
    tx = 280
    s.module(tx, py, 200, 120, "TP4056 (มีวงจรป้องกัน)", ["ชาร์จผ่าน USB บนโมดูล", "รุ่น 6 ขา: B+ B− OUT+ OUT−"], fill="#fdeeee", stroke="#b85450")
    s.mpin(tx, py + 80, "B+", "left"); s.mpin(tx, py + 104, "B−", "left")
    s.mpin(tx + 200, py + 80, "OUT+", "right"); s.mpin(tx + 200, py + 104, "OUT−", "right")
    s.line([(190, py + 80), (tx, py + 80)], C["5v"]); s.line([(190, py + 104), (tx, py + 104)], C["gnd"])
    # INA219
    ix = 570
    s.module(ix, py, 220, 150, "INA219 (ทดสอบ E3)", ["ต่ออนุกรมสายบวก", "วัดกระแสทั้งโหนด"], fill="#f3eefb", stroke="#7b5ea7", dash="6 4")
    s.mpin(ix, py + 80, "VIN+", "left"); s.mpin(ix + 220, py + 80, "VIN−", "right")
    s.line([(tx + 200, py + 80), (ix, py + 80)], C["vbat"])
    # OUT- to ground
    s.line([(tx + 200, py + 104), (tx + 236, py + 104)], C["gnd"]); s.gnd(tx + 236, py + 104)
    # INA219 bottom pins
    for k, (n, col, lab) in enumerate([("VCC", C["3v3"], "+3V3"), ("GND", C["gnd"], None), ("SCL", C["s3"], "SCL"), ("SDA", C["s4"], "SDA")]):
        xx = ix + 30 + k * 53
        s.mpin(xx, py + 150, n, "bottom")
        if lab:
            s.flag(xx, py + 150, lab, col, "down")
        else:
            s.gnd(xx, py + 150)
    # VBAT bus
    vx = ix + 220
    bus_x = vx + 50
    s.line([(vx, py + 80), (bus_x, py + 80)], C["vbat"])
    s.dot(bus_x, py + 80, C["vbat"])
    s.text(bus_x - 4, py + 66, "VBAT", 13, "end", 700, C["vbat"])
    # LDO
    lx = 960
    s.module(lx, py - 40, 230, 92, "LDO 3.3 V (AP2112K / ME6211)", ["≥500 mA, dropout ต่ำ"], fill="#fff2e0", stroke="#d98200")
    s.mpin(lx, py + 20, "VIN", "left"); s.mpin(lx + 230, py + 20, "VOUT", "right")
    s.mpin(lx + 115, py + 52, "GND", "bottom")
    s.line([(bus_x, py + 80), (bus_x, py + 20), (lx, py + 20)], C["vbat"])
    s.gnd(lx + 115, py + 52)
    s.line([(lx + 230, py + 20), (lx + 300, py + 20)], C["3v3"])
    s.dot(lx + 300, py + 20, C["3v3"])
    s.cap_v(lx + 300, py + 20, py + 80, "10µF + 100µF", C["3v3"])
    s.gnd(lx + 300, py + 80)
    s.line([(lx + 300, py + 20), (lx + 430, py + 20)], C["3v3"])
    s.flag(lx + 430, py + 20, "+3V3", C["3v3"], "right")
    s.text(lx + 440, py + 56, "→ ขา 3V3 ของ ESP32 + HX711 + INA219", 12, "start", 400, "#7a4a00")
    # MT3608
    mx = 960
    my = py + 110
    s.module(mx, my, 230, 92, "MT3608 Boost → 5.0 V", ["หมุน trimmer ให้ได้ 5.0 V ก่อนต่อ!"], fill="#fdecec", stroke="#c0392b")
    s.mpin(mx, my + 60, "VIN+", "left"); s.mpin(mx + 230, my + 60, "VOUT+", "right")
    s.mpin(mx + 115, my + 92, "GND", "bottom")
    s.line([(bus_x, py + 80), (bus_x, my + 60), (mx, my + 60)], C["vbat"])
    s.gnd(mx + 115, my + 92)
    s.line([(mx + 230, my + 60), (mx + 300, my + 60)], C["5v"])
    s.flag(mx + 300, my + 60, "+5V", C["5v"], "right")
    s.text(mx + 380, my + 66, "→ VCC ของ HC-SR04 เท่านั้น", 12, "start", 400, "#7a1010")
    # VBAT net flag for sense divider
    s.line([(bus_x, my + 60), (bus_x, my + 120)], C["vbat"])
    s.flag(bus_x, my + 120, "VBAT", C["vbat"], "down")
    s.dot(bus_x, my + 60, C["vbat"])

    # ---------- ESP32 ----------
    top = 480
    s.text(40, top - 24, "② บอร์ดหลักและเซนเซอร์", 16, "start", 700, "#1b2a41")
    used = {
        "D34": 1, "D25": 1, "D26": 1, "D19": 1, "D18": 1, "D21": 1, "D22": 1,
        "GNDL": 1, "GNDR": 1, "3V3": 1,
    }
    pts, y0, bot = esp32(s, top, used, "NODE")
    ry = lambda i: y0 + i * P

    # 3V3 in, GND
    x, y = pts["3V3"]; s.line([(x, y), (x + 40, y)], C["3v3"]); s.flag(x + 40, y, "+3V3 (จาก LDO)", C["3v3"], "right")
    x, y = pts["GNDR"]; s.line([(x, y), (x + 30, y)], C["gnd"]); s.gnd(x + 30, y)
    x, y = pts["GNDL"]; s.line([(x, y), (x - 30, y)], C["gnd"]); s.gnd(x - 30, y)
    x, y = pts["VIN"]; s.text(x - 8, y + 5, "ไม่ต่อ (ห้ามจ่ายแบตเข้า VIN)", 12, "end", 400, "#999", italic=True)

    # VBAT sense divider at D34
    x, y = pts["D34"]
    nx = 640
    s.line([(x, y), (nx, y)], C["vbat"]); s.dot(nx, y, C["vbat"])
    s.resistor_v(nx, y - 90, y, "100kΩ", C["vbat"], "left")
    s.flag(nx, y - 90, "VBAT", C["vbat"], "up")
    s.resistor_v(nx, y, y + 80, "100kΩ", C["gnd"], "right")
    s.gnd(nx, y + 80)
    s.line([(nx, y), (nx - 70, y)], C["vbat"])
    s.cap_v(nx - 70, y, y + 60, "", C["vbat"], polar=False)
    s.text(nx - 90, y + 35, "100nF", 13, "end", 700, "#333")
    s.gnd(nx - 70, y + 60)

    # HC-SR04 (left), signals at D25 (trig) / D26 (echo)
    hx, hw = 110, 220
    hy1, hy2 = ry(6) - 30, ry(9) + 30
    s.module(hx, hy1, hw, hy2 - hy1, "HC-SR04", ["อัลตราโซนิก (5 V)"], fill="#eef4fb", stroke="#4a6fa5")
    # power pins left side
    s.mpin(hx, ry(7), "VCC", "left"); s.flag(hx, ry(7), "+5V", C["5v"], "left")
    s.mpin(hx, ry(8), "GND", "left"); s.line([(hx, ry(8)), (hx - 30, ry(8))], C["gnd"]); s.gnd(hx - 30, ry(8))
    s.mpin(hx + hw, ry(7), "Trig", "right"); s.mpin(hx + hw, ry(8), "Echo", "right")
    x, y = pts["D25"]
    s.line([(hx + hw, ry(7)), (x, y)], C["s1"]); s.wire_label(530, y, "Trig → GPIO25", C["s1"])
    # echo divider: series 1k, 2k to GND
    x, y = pts["D26"]
    s.resistor_h(hx + hw, 520, y, "1kΩ", C["s2"])
    s.dot(560, y, C["s2"])
    s.line([(520, y), (x, y)], C["s2"])
    s.resistor_v(560, y, y + 90, "2kΩ", C["gnd"], "left")
    s.gnd(560, y + 90)
    s.wire_label(660, y, "Echo 3.3V → GPIO26", C["s2"])
    s.text(hx + hw / 2, hy2 + 22, "Echo ออก 5 V → แบ่งแรงดันเหลือ 3.3 V", 12, "middle", 700, "#b00020")

    # right side: I2C flags to INA219
    for pin, lab, col in (("D22", "SCL → INA219", C["s3"]), ("D21", "SDA → INA219", C["s4"])):
        x, y = pts[pin]; s.line([(x, y), (x + 40, y)], col); s.flag(x + 40, y, lab, col, "right")

    # HX711 (right) DT D19, SCK D18
    kx, kw = 1300, 200
    ky1, ky2 = ry(5) - 50, ry(6) + 80
    s.module(kx, ky1, kw, ky2 - ky1, "HX711", None, fill="#eefbf1", stroke="#3c8d4f")
    s.mpin(kx, ry(5), "DT (DOUT)", "left"); s.mpin(kx, ry(6), "SCK", "left")
    x, y = pts["D19"]; s.line([(x, y), (kx, ry(5))], C["s5"]); s.wire_label(1200, y, "DT → GPIO19", C["s5"])
    x, y = pts["D18"]; s.line([(x, y), (kx, ry(6))], C["s6"]); s.wire_label(1200, y, "SCK → GPIO18", C["s6"])
    s.mpin(kx + 28, ky1, "VCC", "top"); s.flag(kx + 28, ky1, "+3V3", C["3v3"], "up")
    s.mpin(kx + 140, ky2, "GND", "bottom"); s.gnd(kx + 140, ky2)
    # load cell pins on right of HX711
    lc = [("E+", C["5v"], "แดง"), ("E−", C["gnd"], "ดำ"), ("A−", C["white"], "ขาว"), ("A+", C["s2"], "เขียว")]
    lcx = 1640
    s.module(lcx, ky1 - 10, 140, ky2 - ky1 + 20, "Load cell", ["1 kg", "(4 สาย)"], fill="#f5f5f5", stroke="#666")
    for k, (n, col, th) in enumerate(lc):
        yy = ky1 + 70 + k * 28
        s.mpin(kx + kw, yy, n, "right")
        s.line([(kx + kw, yy), (lcx, yy)], col)
        s.pad(lcx, yy)
        s.text(lcx + 10, yy + 5, th, 12, "start", 700, col if col != C["white"] else "#666")
    s.text(1780, ky2 + 32, "สีสายมาตรฐาน · ค่าติดลบ → สลับ A+/A−", 11, "end", 400, "#555")

    # legend + notes
    legend(s, 1300, ky2 + 110, [
        ("+5V", C["5v"], None), ("+3V3", C["3v3"], None), ("GND", C["gnd"], None),
        ("VBAT (3.0–4.2 V)", C["vbat"], None), ("สัญญาณ", C["s1"], None)])
    ny = bot + 40
    notes_box(s, 40, ny, 1720, "ข้อควรระวังก่อนต่อจริง (โหนด)", [
        "1) ตั้ง MT3608 ให้ได้ 5.0 V ด้วยมัลติมิเตอร์ก่อนต่อ HC-SR04 (โมดูลใหม่บางตัวออกมาสูงกว่า 5 V มาก)",
        "2) Echo ของ HC-SR04 เป็น 5 V ห้ามต่อตรงเข้า ESP32: ใช้ 1kΩ อนุกรม + 2kΩ ลงกราวด์ (ได้ ≈3.3 V) — ถ้ายืมได้รุ่น HC-SR04P/RCWL-1601 (3.3 V) ให้ต่อ VCC=3V3 แล้วตัดตัวต้านทานกับ MT3608 ออกได้",
        "3) จ่าย 3.3 V จาก LDO เข้าขา 3V3 โดยตรง ไม่ใช่ VIN (เรกูเลเตอร์บนบอร์ด AMS1117 ต้องการ ≥4.3 V) — ตอนเสียบ USB อัปโหลดโค้ด ให้ถอดสาย +3V3 จาก LDO ออกก่อน",
        "4) GPIO34 อ่านแบตผ่านตัวแบ่ง 100k/100k: batt_mv = ADC_mV × 2 (ใช้ ADC1 เพราะ ADC2 ใช้ไม่ได้ขณะเปิดวิทยุ ESP-NOW/Wi-Fi)",
        "5) ก่อน deep sleep: สั่ง HX711 power-down (SCK ค้าง HIGH > 60µs); MT3608 + HC-SR04 ยังกินไฟ ~2–4 mA ตลอด — ถ้าต้องการลดเพิ่ม ใส่ P-MOSFET ตัดไฟ 5V (ตัวเลือก)",
        "6) INA219 ใช้เฉพาะตอนทดสอบ E3 — ถ้าไม่ได้วัด ให้ต่อ OUT+ ของ TP4056 ตรงไปจุด VBAT แทน",
        "7) GND ทุกจุดต้องเป็นจุดเดียวกัน (สัญลักษณ์กราวด์ทุกอันต่อถึงกัน) · HX711 บางรุ่นมีขา VDD แยกจาก VCC ให้ต่อ 3V3 ทั้งคู่ · บอร์ดวงจรอยู่แยกจากถาดรองน้ำ",
    ])
    return s


# =====================================================================
# GATEWAY
# =====================================================================
def gateway(v2=False):
    s = SVG(1800, 1190)
    s.text(40, 46, "วงจรเกตเวย์ (กล่องป้าย) — ต่อสายระดับขา" + ("" if v2 else " · v1 (เดิม, ใช้ LDR)"), 26, "start", 700, "#1b2a41")
    s.text(40, 72, "Hydration coaster · Gateway · ไฟเลี้ยงจาก USB 5 V (อะแดปเตอร์ ≥1 A) · FreeRTOS + AI + Servo", 14, "start", 400, "#555")
    top = 120
    used = {"D35": 1, "D32": 1, "D33": 1, "D27": 1, "D14": 1, "D18": 1, "D2": 1,
            "VIN": 1, "3V3": 1, "GNDL": 1, "GNDR": 1}
    pts, y0, bot = esp32(s, top, used, "GATEWAY")
    ry = lambda i: y0 + i * P
    MX, MW = 250, 230          # left module column
    inner = MX + MW

    # power pins
    x, y = pts["VIN"]; s.line([(x, y), (x - 40, y)], C["5v"]); s.flag(x - 40, y, "+5V (จาก USB)", C["5v"], "left")
    x, y = pts["GNDL"]; s.line([(x, y), (x - 30, y)], C["gnd"]); s.gnd(x - 30, y)
    x, y = pts["3V3"]; s.line([(x, y), (x + 40, y)], C["3v3"]); s.flag(x + 40, y, "+3V3 (ออกจากบอร์ด)", C["3v3"], "right")
    x, y = pts["GNDR"]; s.line([(x, y), (x + 30, y)], C["gnd"]); s.gnd(x + 30, y)

    def left_mod(r1, r2, title, sub, power, sigs, dash=None, fill="#eef4fb", stroke="#4a6fa5"):
        y1, y2 = ry(r1) - 20, ry(r2) + 20
        s.module(MX, y1, MW, y2 - y1, title, sub, fill=fill, stroke=stroke, dash=dash)
        n = len(power)
        for k, (pn, kind) in enumerate(power):
            yy = y1 + (y2 - y1) * (k + 1) / (n + 1)
            s.mpin(MX, yy, pn, "left")
            if kind == "gnd":
                s.line([(MX, yy), (MX - 30, yy)], C["gnd"]); s.gnd(MX - 30, yy)
            else:
                s.flag(MX, yy, "+5V" if kind == "5v" else "+3V3", C[kind], "left")
        for pn, epin, col, lab in sigs:
            x, y = pts[epin]
            s.mpin(inner, y, pn, "right")
            s.line([(inner, y), (x, y)], col, dash=dash)
            s.wire_label((inner + x) / 2 + 10, y, lab, col)

    if not v2:
        used["D34"] = 1
        pts_ok = True
        left_mod(0.6, 3, "LDR KY-018", ["วัดแสง (analog)"], [("+ (กลาง)", "3v3"), ("−", "gnd")],
                 [("S", "D34", C["s1"], "S → GPIO34")])
    left_mod(4, 5, "เสียง KY-038", None, [("+", "3v3"), ("G", "gnd")],
             [("AO", "D35", C["s2"], "AO → GPIO35"), ("DO", "D32", C["s3"], "DO → GPIO32")])
    # emergency button
    x, y = pts["D33"]
    bx = 520
    s.line([(x, y), (bx + 50, y)], C["s5"])
    s.add(f'<circle cx="{bx+50}" cy="{y}" r="4" fill="#fff" stroke="#333" stroke-width="2"/>')
    s.add(f'<circle cx="{bx}" cy="{y}" r="4" fill="#fff" stroke="#333" stroke-width="2"/>')
    s.line([(bx - 4, y - 18), (bx + 54, y - 18)], "#333", sw=3)
    s.line([(bx + 25, y - 18), (bx + 25, y - 30)], "#333", sw=3)
    s.line([(bx - 4, y), (bx - 40, y)], C["gnd"]); s.gnd(bx - 40, y)
    s.text(bx + 95, y + 26, "ปุ่มหยุดฉุกเฉิน (กดติด-ปล่อยดับ)", 12, "middle", 700, "#333")
    s.wire_label(650, y, "BTN → GPIO33", C["s5"])
    left_mod(7.6, 9, "PIR HC-SR501", ["OUT = 3.3 V (ต่อตรงได้)", "ตั้ง jumper = H (repeat)"], [("VCC", "5v"), ("GND", "gnd")],
             [("OUT", "D27", C["s4"], "OUT → GPIO27 (ISR)")], fill="#eefbf1", stroke="#3c8d4f")
    if v2:
        left_mod(10, 11.6, "DHT11 KY-015", ["อุณหภูมิ/ความชื้น (digital)"], [("+", "3v3"), ("−", "gnd")],
                 [("S", "D14", C["s6"], "S → GPIO14")], fill="#fff6e8", stroke="#d98200")
    else:
        left_mod(10, 11.6, "DHT11 KY-015", ["(ตัวเลือก) ใช้แทน/เพิ่มจาก PIR"], [("+", "3v3"), ("−", "gnd")],
                 [("S", "D14", C["s6"], "S → GPIO14")], dash="7 5", fill="#f6f6f6", stroke="#888")

    # right side: servo on D18
    x, y = pts["D18"]
    sx, sw_ = 1220, 230
    sy1, sy2 = y - 70, y + 80
    s.module(sx, sy1, sw_, sy2 - sy1, "Servo SG90", ["ยกป้าย \"ดื่มน้ำหน่อย\""], fill="#fdeeee", stroke="#b85450")
    s.mpin(sx, y, "สัญญาณ (ส้ม)", "left")
    s.line([(x, y), (sx, y)], C["s1"]); s.wire_label(1140, y, "PWM 50Hz → GPIO18", C["s1"])
    s.mpin(sx + sw_, y - 20, "+5V (แดง)", "right")
    s.mpin(sx + sw_, y + 30, "GND (น้ำตาล)", "right")
    cx = sx + sw_ + 40
    s.line([(sx + sw_, y - 20), (cx, y - 20)], C["5v"]); s.dot(cx, y - 20, C["5v"])
    s.line([(cx, y - 20), (cx, y - 60)], C["5v"]); s.flag(cx, y - 60, "+5V (VIN)", C["5v"], "up")
    s.line([(sx + sw_, y + 30), (cx + 60, y + 30)], C["gnd"]); s.dot(cx + 60, y + 30, C["gnd"])
    s.line([(cx, y - 20), (cx + 60, y - 20)], C["5v"])
    s.cap_v(cx + 60, y - 20, y + 30, "470–1000µF/10V", C["5v"])
    s.line([(cx + 60, y + 30), (cx + 60, y + 50)], C["gnd"]); s.gnd(cx + 60, y + 50)
    s.text(sx + sw_ / 2, sy2 + 22, "ตัวเก็บประจุวางชิดขั้วเซอร์โว", 12, "middle", 700, "#b00020")

    # D2 onboard LED
    x, y = pts["D2"]
    s.text(x + 12, y + 5, "LED บนบอร์ด = สถานะ (ไม่ต้องต่อเพิ่ม)", 12, "start", 400, "#555", italic=True)

    legend(s, 1450, ry(8.6), [("+5V", C["5v"], None), ("+3V3", C["3v3"], None), ("GND", C["gnd"], None),
                            ("สัญญาณ", C["s1"], None)] + ([] if v2 else [("ตัวเลือก (ยังไม่ fix)", "#888", "7 5")]))
    notes = V2_NOTES if v2 else V1_NOTES
    notes_box(s, 40, bot + 40, 1720, "ข้อควรระวังก่อนต่อจริง (เกตเวย์)", notes)
    return s


V2_NOTES = [
        "1) KY-038 และ DHT11 เลี้ยงด้วย 3V3 · KY-038 ถ้าเลี้ยง 5V ขา AO จะเกิน 3.3 V และทำ ADC เสียได้",
        "2) KY-038 AO อยู่บน ADC1 (GPIO35) เพราะ ADC2 ใช้ไม่ได้ตอนเปิด Wi-Fi · GPIO35 เป็น input-only ไม่มี pull-up ภายใน",
        "3) KY-038 AO มีไบแอส ≈ครึ่งไฟ: วัดความดังด้วย peak-to-peak ในหน้าต่าง ~50 ms · DO ปรับระดับด้วย trimmer บนโมดูล",
        "4) Servo กินไฟพีค ~500–700 mA: ใช้อะแดปเตอร์ USB ≥1 A + C 470–1000µF ชิดเซอร์โว · ถ้าบอร์ดรีเซ็ต (brownout) ให้แยกไฟ 5V ให้เซอร์โว แต่ต่อ GND ร่วมกับ ESP32",
        "5) ปุ่มหยุดฉุกเฉินต่อ GPIO33 ↔ GND ใช้ INPUT_PULLUP + interrupt ขาลง · PIR OUT เข้า GPIO27 ใช้ interrupt ขาขึ้น (E2) · รอ PIR warm-up ~30–60 วินาที",
        "6) DHT11 อ่านได้ไม่เกิน 1 ครั้ง/วินาที (แนะนำทุก 5–10 วินาที) ใช้ไลบรารี DHT sensor library · โมดูล KY-015 มี pull-up 10k ในตัวแล้ว",
        "7) หลีกเลี่ยง GPIO6–11 (แฟลช), GPIO0/2/5/12/15 (strapping) สำหรับอุปกรณ์ภายนอก · GPIO34 ว่าง (เดิมใช้ LDR) เก็บไว้สำรองได้",
]

V1_NOTES = [
        "1) KY-018 และ KY-038 ต้องเลี้ยงด้วย 3V3 เท่านั้น ถ้าเลี้ยง 5V ขา analog จะเกิน 3.3 V และทำ ADC เสียได้",
        "2) ขา analog ทั้งหมดอยู่บน ADC1 (GPIO32–39) เพราะ ADC2 ใช้ไม่ได้ตอนเปิด Wi-Fi · GPIO34/35 เป็น input-only ไม่มี pull-up ภายใน (โมดูลมีตัวต้านทานในตัวแล้ว)",
        "3) KY-038 AO มีไบแอส ≈ครึ่งไฟ: วัดความดังด้วย peak-to-peak ในหน้าต่าง ~50 ms · DO ปรับระดับด้วย trimmer บนโมดูล",
        "4) Servo กินไฟพีค ~500–700 mA: ใช้อะแดปเตอร์ USB ≥1 A + C 470–1000µF ชิดเซอร์โว · ถ้าบอร์ดรีเซ็ต (brownout) ให้แยกไฟ 5V ให้เซอร์โว แต่ต่อ GND ร่วมกับ ESP32",
        "5) ปุ่มหยุดฉุกเฉินต่อ GPIO33 ↔ GND ใช้ INPUT_PULLUP + interrupt ขาลง · PIR OUT เข้า GPIO27 ใช้ interrupt ขาขึ้น (E2) · รอ PIR warm-up ~30–60 วินาที",
        "6) ถ้าเปลี่ยนจาก PIR เป็น DHT11: ใช้ GPIO14 (ไม่ต้องย้ายขาอื่น) และหาสัญญาณ interrupt อื่นสำหรับ E2 เช่น KY-038 DO (GPIO32) หรือปุ่ม (GPIO33)",
        "7) หลีกเลี่ยง GPIO6–11 (แฟลช), GPIO0/2/5/12/15 (strapping) สำหรับอุปกรณ์ภายนอก — แผนผังนี้ไม่ได้ใช้ขาเหล่านี้",
]


if __name__ == "__main__":
    import sys
    out = sys.argv[1]
    node().save(f"{out}/wiring-node.svg")
    gateway().save(f"{out}/wiring-gateway-v1-ldr.svg")
    gateway(True).save(f"{out}/wiring-gateway.svg")
    print("ok")
