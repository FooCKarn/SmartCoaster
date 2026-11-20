# คู่มือโครงสร้างและการทำงานของ Smart Coaster Web Dashboard

เอกสารนี้อธิบายสถาปัตยกรรมไฟล์ การออกแบบส่วนติดต่อผู้ใช้ (UI/UX) ตรรกะการแสดงผลสถานะ และวิธีการเชื่อมต่อข้อมูลจริงจาก Firebase Realtime Database สำหรับส่วน **Web Dashboard** ของโปรเจ็กต์ **Smart Coaster (ระบบแผ่นรองแก้วอัจฉริยะ)**

---

## 1. โครงสร้างไฟล์ (Directory Structure)

โฟลเดอร์ `dashboard/` ถูกออกแบบให้แยกส่วนระหว่างโครงสร้างหน้าเว็บ (HTML), การตกแต่ง (CSS) และตรรกะการทำงาน (JavaScript) อย่างชัดเจน เพื่อให้ดูแลรักษาและพัฒนาต่อร่วมกับทีมได้ง่าย:

```text
dashboard/
├── index.html          # หน้าหลัก (Page 1): สรุปปริมาณการดื่มน้ำประจำวันและสถานะ Edge AI
├── sensors.html        # หน้ารอง (Page 2): ข้อมูลเซนเซอร์จาก Node/Gateway และสถานะ E1–E5
├── style.css           # ไฟล์สไตล์ชีตกลาง ใช้ร่วมกันทั้ง 2 หน้า (Theme, Layout, Animation)
├── app.js              # ไฟล์ควบคุมการทำงาน: จัดการกราฟ Chart.js, Mock Data และ Firebase
├── config.example.js   # ไฟล์ตัวอย่างสำหรับตั้งค่า Firebase Web SDK
├── config.js           # ไฟล์ตั้งค่า Firebase จริง (ถูก ignore ใน .gitignore ห้าม commit)
└── assets/             # โฟลเดอร์สำหรับเก็บรูปภาพหรือไอคอนเพิ่มเติม
```

---

## 2. การแบ่งหน้าจอและองค์ประกอบ (Page Architecture)

เว็บไซต์มีหน้าหลักและหน้ารอง โดยผู้ใช้สามารถสลับหน้าผ่านแถบเมนูด้านบนขวา (`Navigation Tabs`) ได้ตลอดเวลา ข้อความบนหน้าเว็บใช้ภาษาอังกฤษ ส่วนเอกสารฉบับนี้คงภาษาไทยไว้

### 2.1 หน้าหลัก: Hydration (`index.html`)
หน้า `index.html` เป็นหน้าเริ่มต้นของเว็บไซต์ (รวมถึง Firebase Hosting) และเป็นแท็บหลักในเมนู

มุ่งเน้นการสื่อสารข้อมูลสุขภาพและการตัดสินใจของระบบให้ผู้ใช้เข้าใจได้ทันที:
1. **Topbar & Telemetry Strip:**
   * แสดงชื่อระบบ ป้ายกำกับโหนดที่ใช้งาน (`NODE coaster-01`) เลขลำดับแพ็กเก็ต (`SEQ`) และเวลาที่ได้รับข้อมูลล่าสุด (`UPDATED`)
2. **Daily Hydration Summary (การ์ดซ้ายบน):**
   * **Dynamic Water Glass:** กราฟิกแก้วน้ำพร้อมสเกลบอกปริมาตร (`0.5L` – `2.0L`) ที่ระดับน้ำจะขยับขึ้น-ลงอัตโนมัติตามเปอร์เซ็นต์ปริมาณน้ำดื่มสะสมเทียบกับเป้าหมาย (`GOAL_ML = 2000`)
   * **สถิติสำคัญ:** แสดงเปอร์เซ็นต์ความคืบหน้า ปริมาณน้ำสะสม (`ml`), อัตราการดื่มเฉลี่ย (`ml/hr`), เวลาและปริมาณการจิบล่าสุด รวมถึงน้ำหนักแก้วปัจจุบันบนแท่น (`g`)
3. **Closed-Loop Edge AI Status (การ์ดขวาบน):**
   * แสดงผลการประมวลผลและตัดสินใจแบบวงปิดของ ESP32 Gateway แบ่งเป็น 3 ส่วนย่อย โดยใช้ **สีเป็นตัวบอกระดับการประเมินสถานะ (Severity Level)**:
     * **AI Hydration Risk:** ประเมินความเสี่ยงการดื่มน้ำน้อยเทียบกับอุณหภูมิและความชื้นในห้อง
     * **Desk Presence & Context:** ยืนยันการอยู่ที่โต๊ะ (จาก PIR + Ultrasonic) และตรวจสอบระดับเสียงรบกวน (KY-038)
     * **Actuator & LINE Alert:** แสดงสถานะการยกป้ายเตือน Servo SG90 และการส่งข้อความแจ้งเตือนผ่าน LINE
4. **สถิติ 7 วันและประวัติการดื่มน้ำ (ส่วนล่าง):**
   * **Last 7 Days:** กราฟแท่งรายวัน (สร้างด้วย HTML/CSS ในฟังก์ชัน `renderWeekBars()` ไม่ได้ใช้ Chart.js) แสดงปริมาณการดื่มน้ำ (`ml`) ของ 7 วันก่อนหน้า (ไม่รวมวันนี้) เส้นประคือเป้าหมายรายวัน วางเมาส์บนแท่งจะเห็นวันที่ ปริมาณ และ % ของเป้าหมาย
   * **Recent Drinks (`/drinks`):** รายการการดื่มล่าสุดที่ตรวจจับได้ 6 รายการ

### 2.2 หน้ารอง: Sensors & System (`sensors.html`)
เข้าถึงผ่านแท็บรอง `Sensors & System` โดยแถบเมนูทั้งสองหน้าจะใช้ชื่อ `Hydration` และ `Sensors & System` เหมือนกัน

มุ่งเน้นข้อมูลเชิงวิศวกรรมและการตรวจสอบสุขภาพฮาร์ดแวร์สำหรับผู้พัฒนาและผู้ทดสอบระบบ:
หน้านี้ใช้ layout เดิม (กราฟน้ำหนักซ้าย การ์ดเซนเซอร์ขวา กราฟห้อง + E1–E5 ด้านล่าง) และแยกบอร์ดเจ้าของข้อมูลด้วย **เส้นกั้น** ในคอลัมน์การ์ดเซนเซอร์ (เหนือเส้น = NODE ใต้เส้น = GATEWAY) การ์ดทุกใบมีป้าย `NODE` (สีฟ้าน้ำทะเล) หรือ `GATEWAY` (สีเทา) กำกับ

1. **Load Cell + HX711 Weight Stream** (`NODE`): แสดงน้ำหนักปัจจุบัน (`g`) คู่กับกราฟเส้น (`Chart.js Line Chart`) เพื่อดูจังหวะการยกแก้วดื่มและการเติมน้ำ
   * กราฟเก็บข้อมูลทั้งวัน (00:00 – ปัจจุบัน) เริ่มต้นแสดง 60 ค่าล่าสุด เลื่อนซ้าย-ขวาดูช่วงเวลาอื่นได้ด้วยการลากที่กราฟ ล้อเมาส์ หรือ **แถบเลื่อนใต้กราฟ** (ดูหัวข้อ 2.3) ไม่มีการซูม
2. **การ์ดเซนเซอร์ 6 ใบ แบ่งเป็น 2 กลุ่มด้วยเส้นกั้น:**
   * **เหนือเส้น = NODE** (ESP32 node ใต้แก้ว): Ultrasonic `HC-SR04` ระยะห่างของผู้ใช้ (`cm`) การ์ดเต็มความกว้างของคอลัมน์
   * **ใต้เส้น = GATEWAY** (ESP32 gateway กล่องป้าย): อุณหภูมิ (`°C`) และความชื้นสัมพัทธ์ (`%`) จาก DHT11, ระดับเสียง (`p2p`) จาก KY-038, ความแรงสัญญาณ ESP-NOW (`RSSI dBm`) และจำนวนครั้งที่ ISR ของ PIR (ขา GPIO27) ทำงาน แต่ละใบมีป้าย Online/Offline ของตัวเอง
   * การ์ดเรียงตามขนาดจอ: 2 คอลัมน์ (จอกว้าง, การ์ดสุดท้ายยืดเต็มแถว), 3 คอลัมน์ (จอกลาง), 2 คอลัมน์ (แท็บเล็ตเล็ก) และ 1 คอลัมน์ (มือถือ)
   * ไม่มีการ์ดแบตเตอรี่แล้ว (เอาออก 20 พ.ย. 2026)
3. **Room Environment History (DHT11)** (`GATEWAY`): กราฟแนวโน้มอุณหภูมิและความชื้นในห้อง เริ่มต้นแสดง 30 ค่าล่าสุด และเลื่อนย้อนดูข้อมูลทั้งวันได้ด้วยล้อเมาส์หรือแถบเลื่อนใต้กราฟ (ดูหัวข้อ 2.3)
4. **Embedded Subsystem Status (E1–E5):** ตารางสรุปสถานะการทำงานเชิงลึกของเฟิร์มแวร์ (FreeRTOS, ISR, Deep Sleep, ESP-NOW ACK + Store-and-Forward และ TLS/Auth) แต่ละแถวมีป้ายบอกว่าอยู่บอร์ดไหน: E1, E2, E5 = GATEWAY, E3 = NODE, E4 = NODE + GATEWAY (ลิงก์ระหว่างสองบอร์ด)

### 2.3 แถบเลื่อนใต้กราฟ (Chart Scroll Bar)
อยู่ใต้กราฟน้ำหนักและกราฟห้องในหน้า Sensors & System สร้างโดยฟังก์ชัน `createChartScroller()` ใน `app.js` (สไตล์ใน `style.css` หัวข้อ `.chart-scroll`)
- ความกว้างของแถบสีฟ้า = สัดส่วนช่วงที่เห็นอยู่เทียบกับข้อมูลทั้งหมด ถ้าเห็นข้อมูลทั้งหมดแถบจะเต็มรางและเป็นสีจาง
- ข้อความด้านขวาของแถบบอกช่วงเวลาที่กราฟแสดงอยู่ (จุดแรก – จุดสุดท้าย)
- วิธีเลื่อน:
  - ชี้เมาส์ที่กราฟแล้วหมุนล้อเมาส์ (หรือปัดแทร็กแพดซ้าย-ขวา) ไม่ต้องคลิก ล้อหมุนลง = ไปทางข้อมูลใหม่ ล้อหมุนขึ้น = ย้อนไปข้อมูลเก่า ล้อ 1 คลิกเลื่อน 10% ของช่วงที่เห็น ใช้ได้ทั้งกราฟน้ำหนักและกราฟห้อง และบนแถบเลื่อนด้วย
  - ถ้าเลื่อนไปชนขอบแล้ว (ต้นวัน / ข้อมูลล่าสุด) ล้อจะเลื่อนหน้าเว็บตามปกติ
  - ลากแถบสีฟ้าไปซ้าย/ขวา
  - กดที่ราง แถบจะย้ายไปอยู่ตรงที่กด
  - โฟกัสที่แถบแล้วกดปุ่ม ← → (เลื่อนทีละ 10% ของช่วงที่เห็น) PageUp / PageDown (เลื่อนทีละช่วง) Home / End (ไปต้นวัน / ล่าสุด)
- กราฟน้ำหนัก: ลากที่กราฟเพื่อเลื่อนได้เหมือนกัน แถบจะขยับตาม (กราฟห้องลากเลื่อนที่ตัวกราฟไม่ได้ ใช้ล้อเมาส์หรือแถบ) (ไม่มีการซูม ความกว้างช่วงที่เห็นคงที่ที่ 60 ค่า ตั้งที่ `WEIGHT_DEFAULT_POINTS` ใน `app.js`)
- ถ้าเลื่อนไปอยู่ท้ายสุด (ข้อมูลล่าสุด) กราฟจะตามข้อมูลใหม่ที่เข้ามาต่อไป ถ้าเลื่อนย้อนไปดูช่วงเก่า กราฟจะอยู่ที่ช่วงนั้นไม่เด้งกลับ
- ถ้าข้อมูลของวันมีเกิน 400 จุด กราฟน้ำหนักจะสุ่มลดจุดเหลือไม่เกิน 400 (`renderWeightChart()`) ดังนั้นจุดบนกราฟอาจห่างกว่าข้อมูลจริง

---

## 3. ตรรกะการเปลี่ยนสีตามระดับสถานะ (Dynamic Severity Color Logic)

ในไฟล์ `app.js` ฟังก์ชัน `renderDashboardPage(v)` จะปรับสีขอบการ์ดและกล่องไอคอนของส่วน **Closed-Loop Edge AI Status** เป็น 3 ระดับ (**เขียว / เหลือง / แดง**) โดยอัตโนมัติตามเงื่อนไขดังนี้:

| หัวข้อการประเมิน | 🟢 สีเขียว (`green`) | 🟡 สีเหลือง (`yellow`) | 🔴 สีแดง (`red`) |
| :--- | :--- | :--- | :--- |
| **1. AI Hydration Risk** | `ai_p` ≤ 0.62 (`normal`) | `ai_p` มากกว่า 0.62 แต่น้อยกว่า 0.70 (`moderate`) | `ai_p` ≥ 0.70 (`high`) gateway ยกป้าย |
| **2. Desk Presence** | อยู่ที่โต๊ะ (`0 < dist_cm < 100`) และเสียงปกติ | อยู่ที่โต๊ะ แต่เสียงดัง/กำลังคุย (`sound_p2p > 300`) เลื่อนการเตือนชั่วคราว | ไม่อยู่ที่โต๊ะ (`dist_cm <= 0` หรือ $\ge 100$) งดแจ้งเตือน |
| **3. Actuator & LINE** | ป้าย SG90 พับเก็บ (`sign_up` = false) | ยกป้ายเตือนอยู่ (`sign_up` = true) ถ้าไม่ดื่มภายใน 10 นาที gateway ส่ง LINE | กดปุ่มหยุดฉุกเฉิน (`estop` = true) |

ก่อนข้อมูลจาก `/latest` มาถึง ทั้ง 3 หัวข้อยังไม่มีสี และขึ้นข้อความ `Waiting for data`

ถ้ายังไม่มี `ai_p` (AI ยังไม่ได้คำนวณ) หัวข้อที่ 1 จะใช้เปอร์เซ็นต์การดื่มเทียบเป้าหมายแทน (≥ 75% เขียว, 50–74% เหลือง, น้อยกว่า 50% แดง)

ค่า 0.70 และ 0.62 ตรงกับ `AI_P_UP` และ `AI_P_DOWN` ใน `gateway/gateway.ino` ถ้าแก้ที่ gateway ให้แก้ใน `app.js` ด้วย

---

## 4. ข้อมูลที่อ่านจาก Firebase (Data Schema)

`app.js` อ่าน 3 ที่ใน Realtime Database แล้วแปลงเป็นค่าที่หน้าเว็บใช้ในฟังก์ชัน `toView()`

| path | ใช้ในหน้า | field ที่ใช้ |
|---|---|---|
| `/latest` | ทั้ง 2 หน้า | `node_id`, `seq`, `ts`, `weight_g`, `dist_cm`, `rssi`, `room_t`, `room_rh`, `sound_p2p`, `motion_count`, `ml_today`, `ai_p`, `sign_up`, `estop`, `demo` |
| `/drinks` (7 วันย้อนหลัง) | Hydration | `ts`, `ml` (= น้ำหนักที่หายไป g), `ml_today`, `w_from`, `w_to` (น้ำหนักก่อน/หลังดื่ม) |
| `/refills` (7 วันย้อนหลัง) | Hydration | `ts`, `ml` (น้ำหนักที่เพิ่มขึ้น g), `w_from`, `w_to` |
| `/readings` (ตั้งแต่ 00:00 ของวันนี้) | Sensors & System | `ts`, `weight_g`, `room_t`, `room_rh` |

ค่าที่หน้าเว็บคำนวณเอง:

| ค่าบนหน้าเว็บ | มาจาก |
|---|---|
| ปริมาณดื่มวันนี้ (`daily_ml`) | `ml_today` ใน `/latest` |
| ml/hr (`hourly_rate`) | ผลรวม `ml` ใน `/drinks` ช่วง 60 นาทีล่าสุด |
| ดื่มครั้งล่าสุด (`last_sip_ts`, `last_sip_ml`) | รายการล่าสุดใน `/drinks` |
| กราฟ 7 วัน (`week_ml`) | ผลรวม `ml` ใน `/drinks` แยกตามวัน (ไม่รวมวันนี้) |
| Recent Drinks (`sips`) | 6 รายการล่าสุดจาก `/drinks` รวมกับ `/refills` เรียงตามเวลา (เติมน้ำมีป้าย Refill และไม่นับเป็นปริมาณที่ดื่ม) |
| ระดับ AI (`ai_risk`) | `ai_p` เทียบกับ 0.70 และ 0.62 (ข้อ 3) |
| สถานะป้าย (`alert_state`) | `estop` ก่อน แล้วค่อย `line_sent` แล้วค่อย `sign_up` |
| สัดส่วนแพ็กเก็ตที่ส่งซ้ำ (หน้า Sensors) | `pkts_retried` เทียบกับ `seq` ใน `/latest` |

ค่าจาก gateway ที่ใช้คู่กัน:
- `line_sent` = true เมื่อ gateway ส่ง LINE อัตโนมัติสำเร็จ (ได้ 200) ระหว่างที่ป้ายยกค้างอยู่ รีเซ็ตเป็น false เมื่อป้ายขึ้นหรือลงครั้งใหม่ (ปุ่ม m ใน Serial ไม่นับ)
- `pkts_retried` = จำนวน seq ของ node นี้ที่ส่งครั้งแรกไม่สำเร็จ คือมาถึงพร้อม `age_ms` ≥ 200 ms (ค้างในคิว node) หรือมาซ้ำเพราะ ACK หาย นับต่อ node และเริ่ม 0 ใหม่เมื่อ node เปิดเครื่องใหม่ (`boot_id` เปลี่ยน)
- ถ้า gateway รีสตาร์ตแต่ node ไม่รีสตาร์ต `pkts_retried` จะเริ่ม 0 ใหม่ แต่ `seq` นับต่อ สัดส่วนที่ส่งซ้ำจะต่ำกว่าจริง

---

## 5. วิธีรัน

### 5.1 รันบนเครื่อง
1. เปิด Terminal ที่โฟลเดอร์ `dashboard/`
2. รัน `python -m http.server 8000` (ต้องเปิดผ่าน HTTP เพราะ `app.js` เป็น ES Module เปิดแบบ `file://` ไม่ได้)
3. เปิด `http://localhost:8000`

### 5.2 ข้อมูลจริงกับข้อมูลจำลอง
- มี `dashboard/config.js` → ต่อ Firebase จริงทันที จุดสถานะซ้ายบนเป็นสีเขียวและขึ้น `LIVE FIREBASE`
- ไม่มี `config.js` → แสดง `MOCK_DATA` ใน `app.js` และขึ้น `MOCK DATA MODE`
- ระหว่างรอข้อมูลจริง (ตอนโหลดหน้า) ค่าบนหน้าเว็บเป็น `–` ไม่มีตัวเลขตัวอย่างใน HTML
  - ข้อความสถานะเริ่มต้น: `LOADING...` (Hydration) และ `CONNECTING TO FIREBASE...` (Sensors & System)
  - หน้า Hydration ใช้ข้อความ `LOADING...` เช็กว่า `app.js` ทำงานไหม ถ้าโหลดหน้าเสร็จแล้วข้อความยังเป็น `LOADING...` จะขึ้นคำเตือนให้เปิดผ่าน `http://localhost:8000`
- สร้าง `config.js` โดยคัดลอก `config.example.js` แล้วใส่ `firebaseConfig` จาก Firebase Console (Project settings → Your apps → Web app)

### 5.3 ล็อกอิน (E5)
- ถ้าใช้ rules แบบ test mode อ่านได้เลย ไม่ต้องล็อกอิน
- ถ้าใช้ `firebase/database.rules.json` (ต้องล็อกอิน) หน้าเว็บจะขึ้นฟอร์ม Sign in ให้ใส่อีเมลและรหัสผ่านของบัญชีใน Firebase Authentication

---

## 6. เส้นทางข้อมูล: แต่ละส่วนของ Dashboard มาจากไหน

หัวข้อนี้ไล่ย้อนจากสิ่งที่เห็นบนหน้าเว็บ กลับไปถึงไฟล์และฟังก์ชันที่สร้างค่านั้น (หัวข้อ 4 บอกว่าอ่านอะไรจาก Firebase ส่วนหัวข้อนี้บอกว่าค่านั้นมาจากไหน)

### 6.1 ภาพรวมเส้นทาง

```text
 Node (แผ่นรองแก้ว)                     Gateway (กล่องป้าย)                        Cloud                 Dashboard
┌──────────────────────┐   ESP-NOW    ┌────────────────────────────────┐  HTTPS   ┌──────────────┐  SDK  ┌────────────────┐
│ HX711  → weight_g    │  NodeMsg     │ nodeTask   (gateway.ino)       │  PATCH   │ Firebase RTDB│  อ่าน │ app.js         │
│ HC-SR04→ dist_cm     │ (msg.h) ───▶ │  ├ drink_detect.h → ml / cup   │  /POST   │  /latest     │ ────▶ │  toView()      │
│ แบต    → batt_mv     │              │  └ สร้าง rec → uploadQueue      │ ───────▶ │  /readings   │       │  updateAll()   │
└──────────────────────┘              │ sensorTask: DHT11, KY-038, PIR │ (cloud.h │  /drinks     │       │   ├ index.html │
                                      │ aiTask    : ai_model.h → ai_p  │  + auth) │  /refills    │       │   └ sensors.html│
                                      │ netTask   : Firebase + LINE    │          └──────────────┘       └────────────────┘
                                      └────────────────────────────────┘
```

- Node ไม่ส่งขึ้น Firebase เอง ต้องผ่าน Gateway เสมอ (ไฟล์เฟิร์มแวร์ของ node ไม่อยู่ในชุดไฟล์เอกสารนี้ แต่รูปแบบแพ็กเก็ตที่ส่งกำหนดไว้ใน `common/msg.h`)
- Gateway ล็อกอิน Firebase ด้วยบัญชีอีเมล/รหัสผ่านของตัวเอง (`firebase_auth.h`, ค่าอยู่ใน `secrets.h`) ส่วนหน้าเว็บอ่านด้วย `config.js` และบัญชีผู้ใช้ที่ล็อกอินบนหน้าเว็บ สองส่วนนี้ไม่ได้ใช้ไฟล์ตั้งค่าร่วมกัน
- ถ้า Wi-Fi หรืออินเทอร์เน็ตล่ม Gateway เก็บรายการลง flash (`/queue.txt` ใน `cloud.h`) แล้วทยอยส่งตอนเน็ตกลับมา ค่าเหล่านั้นมาถึงช้าแต่ `ts` ยังเป็นเวลาตอนวัดจริง

### 6.2 ค่าแต่ละตัวถูกสร้างที่ไหน (ฝั่ง Gateway)

ทุกค่าใน `/latest` และ `/readings` ถูกประกอบเป็นก้อน JSON (`rec`) ใน `nodeTask` ของ `gateway.ino` แล้วใส่ `uploadQueue` ให้ `netTask` ส่งต่อ

| field | วัด/คำนวณที่ไหน | ไฟล์ / ฟังก์ชันที่เกี่ยวข้อง |
|---|---|---|
| `node_id`, `seq` | Node ใส่มาในแพ็กเก็ต | `msg.h` (`NodeMsg`) |
| `weight_g` | Node อ่านจาก Load Cell + HX711 | `msg.h` (`NodeMsg.weight_g`) |
| `dist_cm` | Node อ่านจาก HC-SR04 (-1 = ไม่มี echo) | `msg.h` (`NodeMsg.dist_cm`) |
| `batt_mv` | Node วัดแรงดันแบต 18650 | `msg.h` (`NodeMsg.batt_mv`) |
| `age_ms` | Node บอกว่าค่านี้ค้างในคิวมากี่ ms (ส่งซ้ำ) | `msg.h` (`NodeMsg.age_ms`) |
| `rssi` | Gateway อ่านความแรงสัญญาณจากแพ็กเก็ตที่รับ | `gateway.ino` (`nodeTask`, `it.rssi`) |
| `pkts_retried` | Gateway นับแพ็กเก็ตที่มาซ้ำหรือค้างคิว | `gateway.ino` (`linkStatFor()`, `isDuplicate()`) |
| `room_t`, `room_rh` | DHT11 บน Gateway | `gateway.ino` (`sensorTask`) |
| `sound_p2p` | KY-038 บน Gateway (ค่า peak-to-peak 50 ms) | `gateway.ino` (`readSound()`, `sensorTask`) |
| `motion_count` | จำนวนครั้งที่ PIR ทริกเกอร์ (ISR ขา PIR_PIN) | `gateway.ino` (`onMotion()`) |
| `cup` | มีแก้วอยู่บนแท่นหรือไม่ | `drink_detect.h` (`cupPresent()`) |
| `ml_today` | ผลรวมน้ำที่ดื่มวันนี้ จากน้ำหนักที่หายไป (1 g ≈ 1 ml) เก็บลง flash (NVS) กันหายตอนรีบูตและรีเซ็ตตอนขึ้นวันใหม่ | `drink_detect.h` (`DrinkDetector::update()`), `gateway.ino` (`handleDrinkLocked()`, `saveDrinkStateLocked()`) |
| `mins_since_drink` | นาทีนับจากดื่มครั้งล่าสุด | `gateway.ino` (`minsSince()`) |
| `ai_p`, `ai_x` | ความเสี่ยงดื่มน้ำไม่พอใน 60 นาทีข้างหน้า (0–1) และ feature 7 ตัวที่ใช้คำนวณ | `ai_model.h` (`aiPredict()`), `gateway.ino` (`aiTask`) |
| `sign_up` | ป้าย SG90 ยกอยู่หรือไม่ (AI หรือกฎตายตัวสั่ง) | `gateway.ino` (`aiTask`, `setSignLocked()`) |
| `present` | อยู่ที่โต๊ะ = PIR หรือ Ultrasonic เจอคน | `gateway.ino` (`sensorTask`) |
| `estop` | ปุ่มหยุดฉุกเฉิน (ISR) | `gateway.ino` (`onStop()`) |
| `line_sent` | ส่ง LINE อัตโนมัติสำเร็จ (HTTP 200) ระหว่างป้ายยกค้าง | `gateway.ino` (`netTask`), `line_push.h`, ตัดสินใจโดย `line_notify.h` |
| `ts` | เวลาตอนวัด (epoch ms) ถ้ายังไม่ได้เวลา NTP ให้ Firebase ใส่เองด้วย server timestamp | `gateway.ino` (`stampItem()`), `cloud.h` (`epochMs()`) |
| `demo` | มีเมื่อ `DEMO_SPEED` ≠ 1 (โหมดเร่งเวลาถ่ายวิดีโอ) | `gateway.ino` (`stampItem()`) |

รายการใน `/drinks` (และ `/refills`) ถูกสร้างใน `nodeTask` เมื่อ `drink_detect.h` ตรวจพบเหตุการณ์ดื่มหรือเติมน้ำ ประกอบด้วย `ts`, `node_id`, `type`, `ml` (น้ำหนักที่หายไป/เพิ่มขึ้น), `ml_today`, `w_from` และ `w_to` (น้ำหนักก่อนและหลัง) หน้า Hydration อ่านทั้ง `/drinks` และ `/refills` (เติมน้ำแสดงในรายการ Recent Drinks แต่ไม่นับรวมใน `ml_today` ml/hr หรือกราฟ 7 วัน)

### 6.3 แต่ละส่วนของหน้าเว็บอ่านจากไหน

ทุกหน้าโหลด `app.js` ตัวเดียวกัน แล้วแยกการแสดงผลตาม `data-page` ของ `<body>` (`dashboard` หรือ `sensors`) ค่าจาก Firebase ผ่าน `toView()` แล้วส่งให้ `updateAll()` → `renderDashboardPage()` หรือ `renderSensorsPage()`

#### หน้า Hydration (`index.html`)

| ส่วนบนหน้าเว็บ | element id | ค่าที่ใช้ | Firebase path | คำนวณใน `app.js` |
|---|---|---|---|---|
| สถานะ Online/Offline (มุมบนขวา) | `deviceStatusBadge`, `connDot`, `status` | `ts` | `/latest` | `updateDeviceOnlineStatus()` ถ้าไม่มีข้อมูลใหม่เกิน 30 วินาที (`OFFLINE_TIMEOUT_MS`) = Offline |
| SEQ / UPDATED | `seqBadge`, `lastUpdate` | `seq`, `ts` | `/latest` | `updateAll()` |
| แก้วน้ำ, เปอร์เซ็นต์, ml สะสม, ml ที่เหลือ | `waterLevel`, `pctText`, `mlText`, `remainText` | `ml_today` | `/latest` | `daily_ml` ใน `toView()` เทียบกับ `GOAL_ML` (2000) |
| ป้าย ml/hr | `hourlyRateBadge` | `ml`, `ts` | `/drinks` | `drinkStats()` รวม `ml` ของ 60 นาทีล่าสุด |
| Last Drink / Amount | `lastSipTime`, `lastSipMl` | `ts`, `ml` ของรายการล่าสุด | `/drinks` | `drinkStats()` |
| Coaster Weight | `currentWeight` | `weight_g` | `/latest` | แสดงตรงๆ |
| Hydration Risk (การ์ดแรก) | `aiCard`, `aiScore`, `aiStatusText`, `aiSubText` | `ai_p`, `room_t`, `room_rh` | `/latest` | `ai_risk` ใน `toView()` เทียบ `AI_P_UP` / `AI_P_DOWN` (ถ้าไม่มี `ai_p` ใช้เปอร์เซ็นต์เทียบเป้าแทน) |
| Desk Presence (การ์ดที่สอง) | `deskCard`, `deskStatusText`, `deskSubText` | `dist_cm`, `sound_p2p` | `/latest` | `renderDashboardPage()` |
| Notifications (การ์ดที่สาม) | `alertCard`, `alertStatusText`, `alertSubText` | `estop`, `line_sent`, `sign_up` | `/latest` | `alert_state` ใน `toView()` |
| Last 7 Days | `weekBars` | `ml`, `ts` | `/drinks` (ย้อนหลัง 7 วัน) | `week_ml` ใน `toView()` รวมรายวัน ไม่รวมวันนี้ วาดด้วย `renderWeekBars()` |
| Recent Drinks | `sipLogList` | `ts`, `ml`, `w_from`, `w_to` | `/drinks` และ `/refills` (รวมกัน 6 รายการล่าสุด) | `drinkStats()` + `sipRow()` รายการเติมน้ำมีป้าย Refill ถ้าไม่มี `w_from`/`w_to` จะแสดง `ml_today` แทน |

#### หน้า Sensors & System (`sensors.html`)

| ส่วนบนหน้าเว็บ | element id | ค่าที่ใช้ | Firebase path | คำนวณใน `app.js` |
|---|---|---|---|---|
| กราฟน้ำหนัก (Weight Stream) | `chartW` | `ts`, `weight_g` | `/readings` (ตั้งแต่ 00:00 ของวันนี้) | `renderWeightChart()` ถ้าเกิน 400 จุดจะสุ่มเอาบางจุด |
| ตัวเลขน้ำหนักปัจจุบัน | `w` | `weight_g` | `/latest` | แสดงตรงๆ |
| Ultrasonic HC-SR04 | `d`, `dDesc` | `dist_cm` | `/latest` | อยู่ที่โต๊ะเมื่อ `0 < dist_cm < 100` |
| แบตเตอรี่ 18650 | `batt`, `battBar`, `battMv` | `batt_mv` | `/latest` | แปลงเป็น % ด้วยสูตร `(mV − 3300) / (4200 − 3300)` ใน `renderSensorsPage()` |
| อุณหภูมิ / ความชื้น | `t`, `rh`, `rhBar` | `room_t`, `room_rh` | `/latest` | แสดงตรงๆ |
| ระดับเสียง KY-038 | `snd`, `sndBar`, `sndState` | `sound_p2p` | `/latest` | เทียบกับ `SOUND_MAX_P2P` (500) เสียงดังเมื่อ > 300 |
| ความแรงสัญญาณ RSSI | `rssi` | `rssi` | `/latest` | แสดงตรงๆ |
| PIR (จำนวนครั้ง ISR) | `pirCount` | `motion_count` | `/latest` | แสดงตรงๆ |
| กราฟอุณหภูมิและความชื้นย้อนหลัง | `chartRoom` | `room_t`, `room_rh`, `ts` | `/readings` | เก็บข้อมูลทั้งวัน เริ่มต้นแสดง 30 ค่าล่าสุด แล้วเลื่อนย้อนดูได้ด้วยล้อเมาส์หรือแถบเลื่อน |
| ป้าย Online/Offline ของเซนเซอร์แต่ละตัว | `st-hx711`, `st-hcsr04`, `st-batt`, `st-temp`, `st-rh`, `st-sound`, `st-rssi`, `st-pir` | ฟิลด์ของเซนเซอร์นั้นใน `/latest` | `/latest` | `updatePerSensorStatuses()` เช็คว่าค่าเป็นตัวเลขที่ใช้ได้ ถ้าทั้งอุปกรณ์ Offline ป้ายทุกตัวเป็น Offline |
| สัดส่วนแพ็กเก็ตที่ส่งซ้ำ | `lnkRow` ฯลฯ | `pkts_retried`, `seq` | `/latest` | `renderLinkSplit()` ปัจจุบัน `sensors.html` ไม่มี element นี้ ฟังก์ชันจึงไม่แสดงอะไร |
| ตาราง Embedded Subsystem (E1–E5) | `.sys-specs` | ไม่ใช้ข้อมูลจริง | – | เป็นข้อความคงที่ใน `sensors.html` ต้องแก้ด้วยมือถ้าเฟิร์มแวร์เปลี่ยน |

### 6.4 ค่าคงที่ที่ฝังอยู่ใน `app.js`

ถ้าเปลี่ยนที่ Gateway ต้องมาแก้ที่นี่ให้ตรงกัน

| ค่า | ใช้ทำอะไร | ต้องตรงกับ |
|---|---|---|
| `GOAL_ML = 2000` | เป้าหมายดื่มต่อวัน ใช้คำนวณ % แก้วน้ำและกราฟ 7 วัน | ไม่มีฝั่ง Gateway (กำหนดที่ Dashboard เท่านั้น) |
| `AI_P_UP = 0.70`, `AI_P_DOWN = 0.62` | เกณฑ์เปลี่ยนสีระดับความเสี่ยง | `AI_P_UP`, `AI_P_DOWN` ใน `gateway.ino` |
| `OFFLINE_TIMEOUT_MS = 30000` | เกินนี้ถือว่า Offline | ช่วงที่ node ส่ง (ประมาณ 5–10 วินาที) |
| `SOUND_MAX_P2P = 500`, เกณฑ์เสียงดัง 300 | สเกลแถบเสียงและสถานะ "คุยอยู่" | เกณฑ์ตรวจเสียงใน `gateway.ino` |
| สูตรแบต 3300–4200 mV | แปลงเป็นเปอร์เซ็นต์ | ช่วงแรงดันของแบต 18650 |
| ระยะ `dist_cm < 100` | ถือว่าอยู่ที่โต๊ะ | เกณฑ์ presence ใน `gateway.ino` |
| `DRINK_CFG` | ใช้ตรวจจับการดื่มจากน้ำหนักในโหมด Mock เท่านั้น | `DrinkConfig` ใน `drink_detect.h` |

### 6.5 โหมดข้อมูลจำลอง (Mock)

ถ้าไม่มี `config.js` หรือโหลด Firebase SDK ไม่ได้ `runMock()` จะแสดงข้อมูลจาก `MOCK_DATA` แทน ค่าน้ำหนักในโหมดนี้มาจาก `mockWeightSeries()` แล้วคำนวณปริมาณที่ดื่มด้วย `detectDrinks()` ซึ่งใช้หลักการเดียวกับ `drink_detect.h` ค่าอื่นเป็นตัวเลขตายตัวใน `MOCK_DATA`

### 6.6 ถ้าหน้าเว็บแสดงค่าผิดหรือค่าไม่ขึ้น ให้ไล่ตรวจตามลำดับนี้

1. ดู Serial Monitor ของ Gateway ว่ามี `RX node=... seq=... w=...` เข้ามาหรือไม่ (ถ้าไม่มี ปัญหาอยู่ที่ Node หรือ ESP-NOW)
2. ดูว่ามี `[CLOUD] ... -> 200` หรือ error (401 = ล็อกอิน Firebase ไม่ผ่าน, ค่าค้างอยู่ใน `/queue.txt` ถ้าเน็ตล่ม)
3. เปิด Firebase Console ดู `/latest` ว่า field ที่ต้องการมีค่าหรือไม่ และ `ts` อัปเดตหรือเปล่า
4. เปิด Console ของเบราว์เซอร์ (F12) ดู error จาก `app.js` เช่น สิทธิ์อ่านไม่ได้ (ต้องล็อกอิน) หรือ query `/drinks` และ `/readings` ที่ใช้ `orderByChild("ts")` ต้องมีกฎ `.indexOn` ใน rules ถึงจะเร็วและไม่เตือน