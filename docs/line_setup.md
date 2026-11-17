# ระบบแจ้งเตือน LINE ของ SmartCoaster

เอกสารนี้อธิบายภาพรวมการส่ง LINE Broadcast, รูปแบบคำขอ, การทำงานบน Gateway,
การตั้งค่า Channel Access Token และวิธีทดสอบ ระบบปัจจุบันมีตัวส่งข้อความและ
คำสั่งทดสอบด้วย Serial แล้ว แต่ **ยังไม่มีระบบตรวจจับการดื่มหรือกำหนดเวลาเตือน
อัตโนมัติ**.

## ภาพรวม

```text
ผู้ใช้เพิ่ม LINE Official Account เป็นเพื่อน
                    │
                    ▼
             LINE Messaging API
                    ▲
       HTTPS POST + Bearer token
                    │
ESP32 Gateway ── Wi-Fi 2.4 GHz
       ▲
       ├── คำสั่ง Serial `m` (ทดสอบด้วยตนเอง)
       └── ตัวแปร needReminder (จุดเชื่อมสำหรับ logic ในอนาคต)
```

การแจ้งเตือนใช้ **Broadcast API** จึงส่งข้อความเดียวไปยังผู้ใช้ที่เป็นเพื่อนกับ
LINE Official Account ของบอท ไม่ต้องระบุ `LINE_USER_ID` และไม่มีฟิลด์ `to` ใน
JSON request ผู้รับต้องเพิ่มบัญชีบอทเป็นเพื่อนก่อนจึงจะได้รับข้อความ การส่ง
Broadcast อาจใช้โควตาของบัญชีตามเงื่อนไขแพ็กเกจและผู้รับที่ส่งถึง ควรตรวจสอบ
โควตาใน LINE Official Account Manager ก่อนทดสอบจริง

การส่ง LINE เป็นคนละส่วนกับ Firebase: Gateway ส่งข้อมูล sensor ไป Firebase
ตามเดิม ส่วนการแจ้งเตือนส่ง HTTPS ไป `api.line.me` โดยตรง ข้อมูลจาก node,
Firebase และ dashboard ไม่ได้เป็นตัวสั่งการแจ้งเตือนในโค้ดปัจจุบัน

## ลำดับการทำงาน

### ส่ง Broadcast จาก Gateway

1. Gateway เชื่อมต่อ Wi-Fi ในโหมด station ด้วย `WIFI_SSID` และ `WIFI_PASS`.
2. คำสั่ง Serial `m` ตั้ง `needReminder = true` เท่านั้น ไม่ได้เรียก HTTPS
   ภายในส่วนอ่านคำสั่งโดยตรง.
3. ในรอบ `loop()` Gateway ตรวจ `needReminder` และ cooldown 30 นาที
   (`LINE_COOLDOWN_MS`).
4. เมื่อส่งได้ Gateway เรียก
   `lineBroadcast(LINE_TOKEN, "ถึงเวลาดื่มน้ำแล้ว 💧", &response)`.
5. LINE helper สร้าง JSON แล้วส่ง HTTPS POST ไปยัง LINE Messaging API.
6. โค้ดพิมพ์ HTTP status code ทาง Serial; ถ้ามี response body และ status
   ไม่ใช่ 200 ก็พิมพ์รายละเอียดนั้นด้วย.
7. ส่งสำเร็จเมื่อได้ HTTP `200`; ในกรณีนั้น Gateway บันทึกเวลาไว้ใน
   `lastLineMs` เพื่อเริ่ม cooldown.

คำขอ HTTP ทำงานใน `loop()` ไม่ได้ทำใน ESP-NOW receive callback เพราะ network
request ใช้เวลารอได้และไม่ควรบล็อก callback ของ ESP-NOW. อย่างไรก็ตาม HTTP
request ยังบล็อก `loop()` ระหว่างรอผล (timeout ที่ตั้งไว้ 8 วินาที) จึงยังไม่ใช่
ระบบ background task หรือ queue.

### Cooldown และการลองใหม่

- Cooldown เริ่มหลัง Broadcast สำเร็จ และกันการส่งซ้ำเป็นเวลา 30 นาที.
- ถ้าตั้ง `needReminder` ระหว่าง cooldown ตัวแปรจะยังคงเป็น `true`; Gateway
  จะส่งหนึ่งครั้งเมื่อ cooldown หมด ไม่ได้ส่งทันที.
- ถ้าส่งไม่สำเร็จ `needReminder` จะถูกล้าง แต่ `lastLineMs` จะไม่เปลี่ยน
  จึงไม่มีการลองซ้ำอัตโนมัติ ต้องสั่ง `m` ใหม่หลังตรวจสาเหตุ.
- `lastLineMs` เก็บใน RAM; เมื่อรีสตาร์ตบอร์ด cooldown จะเริ่มต้นใหม่.
- คำสั่ง `q` ใช้ตรวจจำนวนข้อความสะสมของเดือนนี้ ไม่ได้ส่งข้อความ.

เมื่อเพิ่มระบบตรวจจับการดื่มในอนาคต ให้ logic นั้นตั้ง `needReminder = true`
และให้การส่งยังทำผ่านส่วนตรวจใน `loop()` เช่นเดิม อย่าเรียก
`lineBroadcast()` จาก ESP-NOW callback หรือ interrupt handler.

### ทดสอบจาก sketch `t10_line`

สเก็ตช์ทดสอบแยกต่างหากใช้ Serial Monitor:

- `s` — ส่ง LINE Broadcast พร้อมข้อความทดสอบ.
- `q` — อ่านจำนวนข้อความที่ใช้ในเดือนนี้.

สเก็ตช์นี้กำหนดช่วงห่างอย่างน้อย 5 วินาทีหลังส่งสำเร็จ เพื่อป้องกันการกดส่งถี่
เกินไป แต่ไม่ใช่ cooldown 30 นาทีของ Gateway firmware.

## โครงสร้างคำขอ LINE

### Broadcast ข้อความ

`lineBroadcast()` ใช้ URL และ JSON รูปแบบนี้:

```http
POST https://api.line.me/v2/bot/message/broadcast
Content-Type: application/json
Authorization: Bearer <LINE_TOKEN>
```

```json
{
  "messages": [
    {
      "type": "text",
      "text": "ถึงเวลาดื่มน้ำแล้ว 💧"
    }
  ]
}
```

ไม่มีฟิลด์ `"to"` เพราะเป็น Broadcast ไปยังเพื่อนของบอททั้งหมด ไม่ใช่ push
ข้อความไปยังผู้ใช้คนเดียว.

### ฟังก์ชันใน `line_push.h`

| ฟังก์ชัน | หน้าที่ | ผลลัพธ์ |
|---|---|---|
| `lineSend(url, token, body, resp)` | ส่ง JSON ด้วย HTTP POST ไปยัง URL ที่ระบุ | HTTP status code, `-1` เมื่อ Wi-Fi ไม่ได้เชื่อมต่อ, `-2` เมื่อเริ่ม HTTP ไม่สำเร็จ |
| `lineBroadcast(token, text, resp)` | สร้าง payload ข้อความและเรียก `lineSend()` ด้วย Broadcast API | HTTP status code จาก LINE |
| `linePush(token, userId, text, resp)` | helper สำรองสำหรับส่งถึงผู้ใช้คนเดียว โดย payload มีฟิลด์ `"to"` | HTTP status code จาก LINE |
| `lineConsumption(token)` | เรียก `GET /v2/bot/message/quota/consumption` และอ่าน `totalUsage` | จำนวนข้อความ หรือ `-1` เมื่อ Wi-Fi, HTTP หรือการอ่าน JSON ล้มเหลว |

`resp` เป็น pointer ที่เลือกส่งได้ ใช้รับ response body จาก POST เมื่อ HTTP
client ได้ status code ที่เป็นบวก ส่วนการ query quota คืนเฉพาะตัวเลข `totalUsage`
หรือ `-1`.

ไฟล์ `common/line_push.h` เป็นสำเนาหลัก และมีสำเนาใน `gateway/` กับ
`tests/t10_line/` เพราะแต่ละ Arduino sketch ต้อง include header จากโฟลเดอร์
ของตัวเอง เมื่อแก้ helper ให้คัดลอกทั้งไฟล์ไปยังอีกสองตำแหน่งให้ตรงกัน.

## ตั้งค่า LINE และ ESP32

### 1. ออก Channel Access Token

1. เปิด LINE Developers Console แล้วเลือก Provider และ Messaging API channel
   ของ LINE Official Account.
2. ในแท็บ **Messaging API** ออกหรือคัดลอก **Channel access token
   (long-lived)**.
3. สแกน QR code ของบัญชีบอท แล้วเพิ่มบอทเป็นเพื่อนจากบัญชี LINE ที่ต้องการให้
   รับ Broadcast.
4. เก็บ token ไว้เป็นความลับ ผู้ที่มี token สามารถส่งข้อความในนามบอทได้.

การใช้ Broadcast ไม่ต้องอ่านหรือกำหนด User ID.

### 2. สร้างไฟล์ secrets เฉพาะเครื่อง

สำหรับ Gateway firmware:

1. คัดลอก `gateway/secrets.example.h` เป็น `gateway/secrets.h`.
2. กำหนดค่า `WIFI_SSID`, `WIFI_PASS` และ `LINE_TOKEN`.
3. กำหนด `DB_URL` และ `DB_AUTH` ตามการตั้งค่า Firebase ที่โปรเจกต์ใช้อยู่
   (ค่านี้ใช้กับ Firebase ไม่ใช่ LINE).

ตัวอย่างเฉพาะค่าที่ LINE ต้องใช้:

```cpp
#define LINE_TOKEN "ใส่ Channel Access Token จริง"
```

สำหรับ sketch ทดสอบ:

1. คัดลอก `tests/t10_line/secrets.example.h` เป็น
   `tests/t10_line/secrets.h`.
2. กำหนด `WIFI_SSID`, `WIFI_PASS` และ `LINE_TOKEN`.

ไฟล์ `secrets.h` ถูก ignore โดย `.gitignore`; อย่า commit ไฟล์นี้หรือใส่ token
จริงในไฟล์ `.example.h`. ตัวอย่าง secrets ใน repository ใช้ค่า placeholder
เท่านั้น.

Gateway และ sketch ทดสอบต้องใช้ Wi-Fi 2.4 GHz ที่ ESP32 เชื่อมต่อได้ และต้องมี
อินเทอร์เน็ตออกไปยัง LINE Messaging API.

### 3. Build และ upload

จากโฟลเดอร์รากของ repository:

```powershell
pio run -e gateway
pio run -e gateway -t upload
```

สำหรับ sketch ทดสอบ:

```powershell
pio run -e t10_line
pio run -e t10_line -t upload
```

เปิด Serial Monitor ที่ `115200` baud. `t10_line` ต้องอัปโหลดลง Gateway board.
Environment `t10_line` ใช้ ArduinoJson v7; environment `gateway` ยังใช้
dependencies ที่ระบุไว้ใน `platformio.ini`.

### 4. ทดสอบ

**Sketch `t10_line`:**

1. เปิด Serial Monitor.
2. พิมพ์ `s` แล้วตรวจข้อความ `LINE broadcast -> 200`.
3. พิมพ์ `q` เพื่อตรวจยอดใช้ข้อความ.

**Gateway firmware:**

1. เปิด Serial Monitor แล้วรอข้อความ `Gateway ready`.
2. พิมพ์ `m` เพื่อสร้าง mock reminder และส่ง Broadcast จริง.
3. ตรวจ `LINE broadcast -> 200`; ถ้าไม่ใช่ 200 ให้ดู response body ที่แสดง
   ต่อจาก status code.
4. พิมพ์ `q` เพื่อ query จำนวนข้อความที่ใช้.

การทดสอบแต่ละครั้งส่งข้อความจริงถึงเพื่อนของบอทและอาจใช้โควตา อย่ากดส่งซ้ำ
โดยไม่จำเป็น.

## ตรวจสอบปัญหา

| สิ่งที่เห็น | ความหมาย/แนวทางตรวจ |
|---|---|
| `LINE broadcast -> 200` | LINE ตอบรับคำขอแล้ว; ตรวจบัญชีผู้รับและสถานะเพื่อนหากไม่เห็นข้อความ |
| `-1` | Wi-Fi ไม่ได้เชื่อมต่อ ณ เวลาส่ง; ตรวจ SSID, password และสถานะเครือข่าย |
| `-2` | HTTP client เริ่มคำขอไม่ได้; ตรวจ URL และการเชื่อมต่อ TLS/เครือข่าย |
| HTTP `401` | token ไม่ถูกต้อง หมดอายุ หรือคัดลอกไม่ครบ; ออก token ใหม่และอัปเดต `secrets.h` |
| HTTP `400` | request ไม่ถูกต้อง; ตรวจ payload, token ที่ตั้ง และข้อความที่ส่ง |
| HTTP `403` | บัญชีหรือสิทธิ์ไม่อนุญาต; ตรวจการตั้งค่า Messaging API และสถานะบัญชี |
| `Failed to read LINE message usage` | Wi-Fi, HTTP response หรือ JSON quota ใช้งานไม่ได้; ตรวจ token และเครือข่าย |
| Build แจ้งหา `secrets.h` ไม่พบ | ยังไม่ได้คัดลอกไฟล์ `secrets.example.h` ไปเป็น `secrets.h` ในโฟลเดอร์ของ sketch นั้น |

HTTP status ที่ไม่ใช่ `200` อาจมี response body จาก LINE เพื่อช่วยวินิจฉัย
หากไม่ปรากฏ body ให้ตรวจ Serial output, Wi-Fi และ token ก่อน.

## ข้อควรระวังด้านความปลอดภัยและข้อจำกัด

- `line_push.h` ใช้ `client.setInsecure()` ในปัจจุบัน ซึ่งเข้ารหัสการเชื่อมต่อ
  แต่ไม่ได้ตรวจสอบ certificate ของ server ใช้สำหรับพัฒนา/ทดสอบเท่านั้น ก่อนใช้
  จริงควรตั้ง root CA ด้วย `client.setCACert(...)`.
- อย่าแสดงหรือ commit Channel Access Token; ใช้ไฟล์ `secrets.h` ที่ถูก ignore.
- `m` เป็น mock trigger ไม่ใช่ระบบตรวจจับการดื่มอัตโนมัติ.
- คำขอ HTTPS เป็น synchronous และอาจทำให้ `loop()` หยุดรอผลสูงสุดตาม timeout
  ปัจจุบัน 8 วินาที.
- ยังไม่มี durable queue, retry อัตโนมัติ, การบันทึก cooldown ลง storage หรือ
  การควบคุมจำนวนผู้รับจากโค้ด.
- `lineConsumption()` แสดงยอดใช้ของ channel ไม่ได้ยืนยันว่าผู้รับแต่ละคนได้รับ
  ข้อความแล้ว.
