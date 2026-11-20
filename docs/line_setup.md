# ระบบแจ้งเตือน LINE ของ SmartCoaster

เอกสารนี้อธิบายภาพรวมการส่ง LINE Push/Broadcast, รูปแบบคำขอ, การทำงานบน Gateway,
การตั้งค่า Channel Access Token และวิธีทดสอบ

> **อัปเดต 19 พ.ย. 2026:** gateway ส่ง LINE อัตโนมัติหลังป้ายยกค้าง 10 นาทีแล้วยังไม่ดื่ม
> ส่งซ้ำได้ทุก 30 นาที สูงสุด 6 ข้อความต่อวัน และส่งใน `netTask` ของ FreeRTOS
> ดูกติกาและเวิร์กโฟลว์ร่วมกับ AI ได้ใน [ai-and-drinks.md](ai-and-drinks.md)

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
        ├── AI/กฎตายตัวสร้าง LineNotifyInput
       └── aiTask → lineQueue → netTask → LINE
```

การแจ้งเตือนใช้ **Broadcast API** จึงส่งข้อความเดียวไปยังผู้ใช้ที่เป็นเพื่อนกับ
LINE Official Account ของบอท ไม่ต้องระบุ `LINE_USER_ID` และไม่มีฟิลด์ `to` ใน
JSON request ผู้รับต้องเพิ่มบัญชีบอทเป็นเพื่อนก่อนจึงจะได้รับข้อความ การส่ง
Broadcast อาจใช้โควตาของบัญชีตามเงื่อนไขแพ็กเกจและผู้รับที่ส่งถึง ควรตรวจสอบ
โควตาใน LINE Official Account Manager ก่อนทดสอบจริง

การส่ง LINE เป็นคนละส่วนกับ Firebase: Gateway ส่งข้อมูล sensor ไป Firebase
ตามเดิม ส่วนการแจ้งเตือนส่ง HTTPS ไป `api.line.me` โดยตรง ข้อมูลจาก node,
Firebase และ dashboard ไม่ได้เป็นตัวสั่งการแจ้งเตือนในโค้ดปัจจุบัน
ตัวตัดสินใจอัตโนมัติอยู่บน gateway และทำงานจากสถานะป้าย, การดื่ม, AI risk,
อุณหภูมิห้องและเวลาในรอบปัจจุบัน

## ลำดับการทำงาน

### ส่งข้อความอัตโนมัติจาก Gateway

1. Gateway เชื่อมต่อ Wi-Fi ในโหมด station ด้วย `WIFI_SSID` และ `WIFI_PASS`.
2. `aiTask` คำนวณ AI ทุก 10 วินาที แล้วส่งค่าขึ้นมาใน `LineNotifyInput`.
3. `LineNotifier` ตรวจว่าป้ายกำลังยกอยู่ ไม่อยู่ช่วงเวลาเงียบ และยังไม่เกิน
   เพดานข้อความต่อวัน.
4. **ยกธงปุ๊บแจ้งทันที:** ในรอบ AI ที่ AI/กฎตายตัวเพิ่งสั่งยกป้าย (`wasUp` เป็น false แล้ว `signUp` เป็น true) `LineNotifier::decideRaise()` อนุญาตให้ส่งข้อความสั้น 1-2 บรรทัดทันที สร้างด้วย `lineBuildRaiseText()` บรรทัดแรกบอกว่ายกธงและ AI ประเมินเสี่ยงกี่ % บรรทัดที่สองบอกเหตุผล ("เพราะ: …") จาก feature ที่ดันความเสี่ยงมากสุด 2 อันดับ หรือถ้ายกเพราะไม่ได้ดื่มเกิน `MAX_NO_DRINK_MIN` จะบอกจำนวนนาทีที่ไม่ได้ดื่ม ไม่ส่งตอนยกป้ายด้วยมือจาก Serial ห่างจากข้อความแจ้งทันทีครั้งก่อนอย่างน้อย `LINE_MIN_GAP_MIN`, นับรวมเพดาน `LINE_MAX_PER_DAY`, ส่งพลาดไม่ลองใหม่ และไม่กระทบจังหวะข้อ 5 ด้านล่าง
5. ป้ายยกค้างครบ `LINE_AFTER_MIN` = 10 นาที (คูณ `DEMO_SPEED`) จะสร้างข้อความ
   GENTLE ครั้งแรก โดยมีเวลาไม่ดื่ม, ยอดดื่ม, อุณหภูมิห้องและความเสี่ยง AI ในข้อความ.
6. ถ้าไม่ดื่มเกิน `MAX_NO_DRINK_MIN` = 90 นาที หรือ AI risk ตั้งแต่ 85% ขึ้นไป
   ข้อความจะเป็น URGENT.
7. หากส่งได้แต่ละครั้งจะถูกรวมในรอบนี้ด้วย `lineQueue` เพื่อให้ `netTask`
   เรียก `linePush()` หรือ `lineBroadcast()` จาก task network.
8. ส่งสำเร็จเมื่อได้รับ HTTP `200`; เฉพาะการส่งสำเร็จเท่านั้นที่นับวัน, cooldown
   และจำนวนข้อความในรอบป้ายยก.

ข้อความถูกสร้างด้วยฟังก์ชัน `lineBuildText()` ใน `gateway/line_notify.h`
และส่งจาก `netTask` ใน `gateway/gateway.ino` ไม่เรียก HTTPS จาก ESP-NOW callback
หรือ interrupt handler

### Cooldown, retry และการยกเลิก

- ส่งครั้งแรกหลังป้ายยกค้างครบ 10 นาที; ครั้งต่อไปห่างอย่างน้อย 30 นาที
  (`LINE_REPEAT_MIN`) และทุกข้อความห่างกันอย่างน้อย 20 นาที (`LINE_MIN_GAP_MIN`)
  เพราะค่า repeat 30 นาทีผ่าน min-gap อยู่แล้ว แต่ min-gap ถือเป็นเพดานล่างทุกระดับ.
- ส่งอัตโนมัติสูงสุด 6 ข้อความต่อวัน (`LINE_MAX_PER_DAY`) และรีเซ็ตเมื่อตรง `tm_yday` เปลี่ยนวัน.
- การส่งล้มเหลวจะลองใหม่หลัง 1 นาที สูงสุด 3 ครั้งต่อข้อความ; หลังลองครบจะถือว่า
  รอบนี้ใช้โควตาไปแล้วและหยุดส่งในช่วงนั้น.
- ถ้ารออยู่ในคิวแล้วเกิดการดื่ม ป้ายลง หรือเข้าโหมดเงียบ `netTask` จะยกเลิกข้อความ
  ไม่นับสำเร็จและไม่ส่งข้อความที่ล้าสมัย.
- คิวมีขนาด 2 รายการ และคิวเต็มจะยกเลิกข้อความโดยไม่เปลี่ยนสถานะ pending.
- ตัวนับส่งสำเร็จและ cooldown เก็บใน RAM จึงรีเซ็ตเมื่อ gateway รีบูต.
- คำสั่ง `l` แสดงยอดส่งวันนี้/รอบนี้และสถานะ pending; `q` ใช้ตรวจโควตาเดือนนี้
  ไม่ได้ส่งข้อความ.

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
4. ถ้าต้องการส่งถึงบัญชีเจ้าของคนเดียว ให้กำหนด `LINE_USER_ID` เป็น LINE user ID;
   ถ้าไม่กำหนด Gateway จะใช้ Broadcast ส่งถึงทุกเพื่อนของ Official Account.

ตัวอย่างเฉพาะค่าที่ LINE ต้องใช้:

```cpp
#define LINE_TOKEN "ใส่ Channel Access Token จริง"
#define LINE_USER_ID "ใส่ LINE User ID หรือปล่อยว่างเพื่อ Broadcast"
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
2. พิมพ์ `m` เพื่อสร้างข้อความทดสอบและส่งจริง.
3. ตรวจ `[LINE] push -> 200` หรือ `[LINE] broadcast -> 200`;
   ถ้าไม่ใช่ 200 ให้ดู response body ที่แสดง
   ต่อจาก status code.
4. พิมพ์ `q` เพื่อ query จำนวนข้อความที่ใช้.
5. พิมพ์ `l` เพื่อดูจำนวนส่งวันนี้, จำนวนในรอบป้ายยก, pending และปลายทาง.
6. พิมพ์ `w` เพื่อดูเหตุผล AI และใช้ `u`/`n` ทดสอบยก/ลดป้ายได้

การทดสอบแต่ละครั้งส่งข้อความจริงถึงเพื่อนของบอทและอาจใช้โควตา อย่ากดส่งซ้ำ
โดยไม่จำเป็น.

## ตรวจสอบปัญหา

| สิ่งที่เห็น | ความหมาย/แนวทางตรวจ |
|---|---|
| `[LINE] broadcast -> 200` หรือ `[LINE] push -> 200` | LINE ตอบรับคำขอแล้ว; ตรวจบัญชีผู้รับและสถานะเพื่อนหากไม่เห็นข้อความ |
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
- `m` เป็น mock trigger สำหรับทดสอบ ไม่ไปรอเงื่อนไขของ AI และไม่ส่ง Broadcast
  จาก ESP-NOW callback.
- คำขอ HTTPS ทำใน `netTask` เป็น synchronous และอาจรอสูงสุด 8 วินาทีใน task นี้
  แต่ไม่บล็อก callback หรือ task ตรวจเซนเซอร์.
- มี retry อัตโนมัติสูงสุด 3 ครั้งต่อข้อความและเพดาน 6 ข้อความต่อวัน แต่ยังไม่มี
  durable LINE queue หรือการบันทึก cooldown ลง flash/NVS.
- `lineConsumption()` แสดงยอดใช้ของ channel ไม่ได้ยืนยันว่าผู้รับแต่ละคนได้รับ
  ข้อความแล้ว.
