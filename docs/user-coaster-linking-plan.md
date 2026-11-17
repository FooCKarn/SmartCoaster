# แผนเชื่อมผู้ใช้ Firebase กับ SmartCoaster

เอกสารนี้เป็นแผนพัฒนาในอนาคต ยังไม่ใช่คำอธิบายระบบที่ทำงานครบแล้ว ปัจจุบัน
Gateway ส่งข้อมูลรวมไปที่ `/readings` และ `/latest`; ยังไม่มีการเข้าสู่ระบบ
Firebase Auth ใน dashboard, การจับคู่เจ้าของต่อ coaster หรือการแยกสิทธิ์อ่าน
ข้อมูลตามเจ้าของ.

## เป้าหมายและสมมติฐาน

### เป้าหมาย

- ผู้ใช้เข้าสู่ระบบด้วย Firebase Authentication และเห็นเฉพาะ coaster ที่ตนมี
  สิทธิ์ดู.
- เชื่อม coaster แต่ละเครื่องกับผู้ใช้ Firebase ได้อย่างตั้งใจและตรวจสอบย้อนหลังได้.
- Gateway เขียนข้อมูลได้เฉพาะ coaster ที่ได้รับมอบหมาย.
- การยกเลิกการเชื่อมต่อหรือโอน coaster ไม่เปิดช่องให้เจ้าของเดิมอ่านข้อมูลใหม่.
- รองรับการมี coaster หลายเครื่องต่อผู้ใช้ โดยเริ่มจากหนึ่งเจ้าของหลักต่อ coaster.
- ถ้าจะส่ง LINE รายบุคคลภายหลัง ให้เลือกผู้รับตามเจ้าของและการยินยอม ไม่ส่ง
  Broadcast ให้ผู้ใช้ทุกคนของ Official Account.

### ขอบเขตที่แนะนำสำหรับรุ่นแรก (MVP)

- หนึ่ง coaster มีเจ้าของหลักได้หนึ่ง Firebase UID ในเวลาเดียวกัน.
- ผู้ใช้หนึ่งคนมี coaster ได้หลายเครื่อง.
- การแชร์ coaster ให้ผู้ใช้หลายคนและ role เช่น caregiver/read-only ทำเป็นระยะ
  ถัดไป โดยไม่เปลี่ยน identity หลักของ coaster.
- “ผู้ใช้เชื่อมกับ Firebase” หมายถึงผู้ใช้ที่ผ่าน Firebase Authentication;
  Firebase Web API key/config เพียงอย่างเดียวไม่ใช่การยืนยันตัวตน.
- `node_id` ในแพ็กเก็ตปัจจุบันเป็นเลขเล็กสำหรับระบุ node ในระบบ ESP-NOW
  ไม่ใช่ device identity ที่ไม่ซ้ำกันทั่วทั้งระบบ.

## สถานะระบบปัจจุบัน

| ส่วน | สถานะปัจจุบัน | ผลต่อการทำ multi-user |
|---|---|---|
| ESP-NOW | Node ส่ง `NodeMsg` ให้ Gateway มี `node_id`, `seq`, น้ำหนัก และระยะ | ยังไม่มี `coasterId` ที่ระบุเครื่องไม่ซ้ำทั่วระบบ |
| Gateway → Firebase | ส่งข้อมูลลง `/readings` และเขียนค่าล่าสุดที่ `/latest` | ข้อมูลจากทุก Gateway อยู่ path ร่วมกัน ไม่มี owner boundary |
| Gateway authentication | โค้ดใช้ `DB_AUTH` เป็น Firebase ID token ใน query string | ต้องจัด identity ต่ออุปกรณ์และการต่ออายุ token ให้ปลอดภัย |
| Dashboard | อ่าน `/latest` และ `/readings` ด้วย Firebase Web SDK | ยังไม่มีหน้า login และ query ยังไม่แยกตามผู้ใช้ |
| Database rules | `.read` อนุญาตทุก `auth != null`; `.write` จำกัด UID เดียวไว้ระดับ root | ไม่ได้แยกข้อมูลของผู้ใช้ และ rule ระดับ root ไม่เหมาะกับ production multi-user |
| LINE | helper มี broadcast ไปหาเพื่อนของบอททั้งหมด | ไม่ควรใช้ broadcast เป็นการแจ้งเตือนส่วนตัวของเจ้าของแต่ละ coaster |

ไฟล์ `firebase/database.rules.test-mode.json` เปิดอ่าน/เขียนทั้งหมด ใช้เฉพาะ
ฐานข้อมูลทดสอบชั่วคราวและห้าม deploy เป็น production.

## โครงสร้างข้อมูลที่เสนอ

ใช้ `coasterId` ที่ไม่ซ้ำและคงที่เป็นตัวตนของชุด node + gateway หนึ่งชุด เช่น
รหัสที่ provision ตอนประกอบเครื่อง อย่าใช้ `node_id = 1` เป็น global ID เพราะ
node หลายชุดอาจใช้เลขเดียวกันได้.

```text
/users/{uid}/coasters/{coasterId}
    role: "owner"
    linkedAt: <server timestamp>

/coasters/{coasterId}
    ownerUid: "{uid}"
    status: "active"
    gatewayId: "{gatewayId}"
    createdAt: <server timestamp>
    linkedAt: <server timestamp>

/coasterData/{coasterId}/latest
    node_id, seq, weight_g, dist_cm, batt_mv
    rssi, room_t, room_rh, sound_p2p, motion_count, ts

/coasterData/{coasterId}/readings/{readingId}
    node_id, seq, weight_g, dist_cm, batt_mv
    rssi, room_t, room_rh, sound_p2p, motion_count, ts

/coasterData/{coasterId}/drinks/{eventId}
    ...drink event data (เพิ่มเมื่อมี drink detection)
```

`/coasters/{coasterId}/ownerUid` เป็นแหล่งอ้างอิงหลักของเจ้าของ ส่วน
`/users/{uid}/coasters/{coasterId}` เป็น index เพื่อให้ dashboard โหลดรายการ
coaster ของผู้ใช้ได้เร็ว การเพิ่ม/ลบสองตำแหน่งนี้ต้องทำผ่าน trusted backend
แบบ atomic multi-location update เพื่อไม่ให้ owner กับ index คลาดกัน.

อย่าเก็บชื่อ, อีเมล, LINE token หรือข้อมูลส่วนตัวไว้ซ้ำในทุก reading; เก็บ UID
ใน metadata แล้วใช้ security rules เป็นตัวจำกัดการอ่าน. Reading ใช้ server
timestamp เพื่อจัดลำดับและแสดงเวลา.

## การแยกตัวตนและสิทธิ์

ระบบมีตัวตนสองชนิดที่ต้องแยกกัน:

1. **ผู้ใช้** — Firebase Authentication UID สำหรับ login เข้า dashboard และดู
   ข้อมูลของตน.
2. **อุปกรณ์** — identity ของ Gateway/coaster สำหรับส่ง telemetry ของอุปกรณ์
   ที่ได้รับอนุญาต.

Firebase Auth UID ของผู้ใช้ไม่ควรถูกใส่ไว้ใน firmware เพื่อใช้เป็นเจ้าของ
อุปกรณ์ และ client ฝั่งผู้ใช้ห้ามเขียน `ownerUid`, `gatewayId` หรือ readings
ของอุปกรณ์โดยตรง.

แนวทาง production ที่แนะนำ:

- ผู้ใช้ login ด้วย Firebase Authentication ผ่าน Firebase Web SDK.
- Backend ที่เชื่อถือได้ (เช่น Cloud Functions/Cloud Run พร้อม Firebase Admin
  SDK) ทำ pairing, เปลี่ยนเจ้าของ, ออก/ตรวจสิทธิ์อุปกรณ์ และจัดการ LINE.
- Gateway มี credential เฉพาะเครื่อง ห้ามฝัง service-account private key หรือ
  Admin SDK credentials ใน firmware.
- เลือกได้ระหว่างให้ Gateway เขียน RTDB ด้วย device identity/custom claims ที่
  จำกัด `coasterId` เดียว หรือให้ Gateway ส่ง telemetry ไป backend แล้ว backend
  เขียน RTDB. เลือกแบบใดแบบหนึ่งก่อนทำ rules; ห้ามใช้ shared gateway UID ที่
  เขียนข้อมูลของทุก coaster ได้.
- Firebase Security Rules ต้องอนุญาตอ่านข้อมูลตาม
  `auth.uid === ownerUid` (หรือ membership role ในระยะที่รองรับการแชร์) และ
  อนุญาตเขียน telemetry เฉพาะ device identity ที่ผูกกับ `coasterId` ใน path.
- ไม่ตั้ง `.read` หรือ `.write` กว้างที่ root เพราะสิทธิ์ที่อนุญาตใน path แม่
  จะครอบคลุมลูกและทำให้การแยกข้อมูลย่อยไม่มีผลตามที่คาด.

ตัวอย่างเงื่อนไขเชิงแนวคิด ไม่ใช่ rules ที่ deploy ได้ทันที:

```text
ผู้ใช้อ่าน /coasterData/{coasterId}
    เมื่อ auth.uid เท่ากับ /coasters/{coasterId}/ownerUid

Gateway เขียน /coasterData/{coasterId}
    เมื่อ token ระบุ role=device และ coasterId ใน token
    เท่ากับ coasterId ใน path

ผู้ใช้แก้ /coasters/{coasterId}/ownerUid ไม่ได้โดยตรง
```

ก่อนเปิดใช้จริง ต้องทดสอบ rules ด้วย Firebase Emulator Suite ทั้งกรณี allow
และ deny; อย่าใช้ตัวอย่างเชิงแนวคิดนี้แทน rules ที่ผ่านการทดสอบ.

## ขั้นตอนจับคู่ coaster กับผู้ใช้

1. **Provision อุปกรณ์:** กำหนด `coasterId` และ `gatewayId` ไม่ซ้ำ บันทึกใน
   registry ฝั่ง backend และผูก identity ของ Gateway เข้ากับ `coasterId`.
2. **เริ่มจับคู่:** ผู้ใช้ login ใน dashboard แล้วสแกน QR หรือกรอกรหัส pairing
   ของ coaster. รหัสต้องมี entropy สูง, ใช้ได้ครั้งเดียว, มีวันหมดอายุ และไม่ใช่
   serial number ที่เดาได้.
3. **ตรวจสอบฝั่ง backend:** backend ตรวจว่ารหัสถูกต้อง ยังไม่หมดอายุ และยังไม่
   ถูกใช้ จากนั้นยืนยันสิทธิ์การ claim ตามนโยบายสินค้า.
4. **บันทึกแบบ atomic:** backend บันทึก owner/index, วันเวลาจับคู่, สถานะ
   coaster และ audit event พร้อม mark รหัสว่าใช้แล้ว.
5. **แสดงผล:** dashboard โหลด `/users/{uid}/coasters` แล้ว subscribe เฉพาะ
   `/coasterData/{coasterId}` ที่ได้รับอนุญาต.
6. **ยกเลิก/โอน:** ทำผ่าน backend ที่ตรวจผู้ร้องขอ, เพิกถอนสมาชิกเดิม, อัปเดต
   owner/index และบันทึก audit. หากมีการโอน ให้หยุดการเข้าถึงข้อมูลใหม่ของ
   เจ้าของเดิมทันที.

อย่าให้หน้าเว็บกำหนด owner ด้วยการเขียน `ownerUid` ตรง ๆ และอย่าใช้รหัส pairing
ถาวรที่พิมพ์อยู่บนเครื่องโดยไม่มีการหมดอายุ/ยกเลิก. กลไกออก QR, การกดปุ่ม
pairing และการกู้คืนเครื่องเป็นรายละเอียดผลิตภัณฑ์ที่ต้องเลือกก่อนพัฒนา
hardware flow.

## แผนพัฒนาเป็นระยะ

### ระยะ 0 — ตกลงนโยบายผลิตภัณฑ์

- ยืนยันว่า MVP ใช้หนึ่ง owner ต่อ coaster และกำหนดว่าจะรองรับการโอนเครื่อง
  หรือไม่.
- ตกลงว่าจะเก็บ readings ไว้กี่วัน และเจ้าของเก่ายังขอ export/delete ข้อมูลได้
  หรือไม่.
- เลือก login provider ของ Firebase Auth และวิธีออก pairing code.
- เลือก device auth: Gateway เขียน RTDB โดยตรงด้วย identity เฉพาะเครื่อง หรือ
  ส่งผ่าน backend.

**เกณฑ์ผ่าน:** ขอบเขตและเจ้าของข้อมูลชัดเจนก่อนกำหนด rules/schema.

### ระยะ 1 — กำหนด identity และ schema

- เพิ่ม registry ของ `coasterId`/`gatewayId`; ระบุว่า `node_id` มีขอบเขตเฉพาะ
  เครือข่าย ESP-NOW ไม่ใช่รหัสเจ้าของ.
- กำหนด schema `/users`, `/coasters`, `/coasterData` และรูปแบบ reading/event.
- วางแผนเพิ่ม `coasterId` ใน provisioning ของ Gateway; ถ้าจะส่งผ่านแพ็กเก็ต
  ESP-NOW ให้ตรวจ compatibility/version ของ `NodeMsg` ก่อนเปลี่ยน.
- กำหนด audit fields: `linkedAt`, `unlinkedAt`, actor และเหตุผลการโอน โดยไม่
  เขียนข้อมูลเกินจำเป็น.

**เกณฑ์ผ่าน:** อุปกรณ์สองชุดที่มี `node_id` เหมือนกันยังแยกข้อมูลกันได้.

### ระยะ 2 — Firebase Auth สำหรับผู้ใช้

- เปิด Firebase Authentication provider ที่เลือก.
- เพิ่มหน้า login/logout และแสดงสถานะผู้ใช้ใน dashboard.
- ป้องกันการแสดงข้อมูลก่อน Auth state ถูกตรวจสอบเสร็จ.
- เพิ่มการจัดการ session หมดอายุ, logout และบัญชีถูกปิดใช้งาน.

**เกณฑ์ผ่าน:** ผู้ใช้ที่ยังไม่ login อ่านข้อมูลส่วนตัวไม่ได้ และ dashboard แสดง
UID/account ที่ login อยู่ได้โดยไม่แสดงข้อมูลของคนอื่น.

### ระยะ 3 — Pairing backend และการจัดการเจ้าของ

- เพิ่ม trusted endpoint/function สำหรับ claim, unclaim, transfer และ revoke.
- ใช้ pairing code แบบสุ่ม ใช้ครั้งเดียว หมดอายุได้ และป้องกัน brute-force ด้วย
  rate limit.
- เขียน owner/index/audit แบบ atomic; client ห้ามแก้ metadata เหล่านี้เอง.
- กำหนดวิธี recovery เมื่อผู้ใช้ย้ายบัญชีหรืออุปกรณ์ถูกขาย/สูญหาย.

**เกณฑ์ผ่าน:** pairing code ใช้ซ้ำหรือหมดอายุไม่ได้; ผู้ใช้ทั่วไปปลอม owner
หรือ claim coaster ของคนอื่นไม่ได้.

### ระยะ 4 — Device authentication และ database rules

- ออก identity ให้ Gateway แต่ละเครื่องและจำกัดสิทธิ์เขียนไว้เฉพาะ
  `coasterId` ของเครื่องนั้น.
- จัดการ token expiry/refresh โดยไม่ฝัง service credentials ใน firmware.
- ย้าย writes จาก `/readings` กับ `/latest` ไปยัง
  `/coasterData/{coasterId}/...`.
- ใช้ atomic update สำหรับ reading และ latest หรือให้ backend จัดการการเขียน
  เพื่อป้องกันข้อมูล history กับ latest ไม่ตรงกัน.
- เขียน rules ใหม่แบบ path-scoped และเพิ่ม Firebase Emulator tests สำหรับ
  ผู้ใช้, device และ unauthenticated client.

**เกณฑ์ผ่าน:** Gateway A เขียน coaster B ไม่ได้; ผู้ใช้ A อ่าน coaster B ไม่ได้;
ผู้ใช้แก้ owner/device assignment ไม่ได้; การเรียกแบบ unauthenticated ถูกปฏิเสธ.

### ระยะ 5 — Dashboard แสดงข้อมูลตามผู้ใช้

- หลัง login โหลดรายการ coaster จาก `/users/{uid}/coasters`.
- ให้ผู้ใช้เลือก coaster ที่ต้องการดู และ subscribe เฉพาะ latest/readings ของ
  เครื่องนั้น.
- รองรับหลาย coaster, สถานะไม่มีเครื่อง, ถูก unpair และ permission denied.
- ยกเลิกการอ่าน root `/latest` และ `/readings` เมื่อย้ายข้อมูลเสร็จ.

**เกณฑ์ผ่าน:** ทดสอบด้วยสองบัญชีที่มีคนละ coaster และยืนยันว่าไม่มีข้อมูล
ข้ามบัญชีทั้งหน้า live และกราฟย้อนหลัง.

### ระยะ 6 — แจ้งเตือนเฉพาะเจ้าของ (ถ้าต้องการ LINE ส่วนตัว)

- การเป็น Firebase user ไม่ได้ทำให้ระบบทราบ LINE account ของคนนั้นโดยอัตโนมัติ;
  ต้องมี account-linking/consent flow แยกต่างหาก เช่น LINE Login/LIFF ที่ผ่าน
  backend.
- เก็บ LINE recipient mapping และ notification preferences ใน backend/พื้นที่
  ที่ rules จำกัด ไม่ให้ Gateway หรือผู้ใช้อื่นอ่าน token/ข้อมูลส่วนตัวได้.
- ให้ backend เลือก recipient จาก owner ของ `coasterId` และการยินยอมก่อนส่ง
  LINE push รายบุคคล.
- เก็บ LINE channel secret/access token เฉพาะ backend; ห้ามใส่ใน ESP32 หรือ
  dashboard.
- ยกเลิกการใช้ LINE Broadcast สำหรับแจ้งเตือนที่เกี่ยวข้องกับสุขภาพ/พฤติกรรม
  เฉพาะคน เพราะ Broadcast ส่งถึงเพื่อนของบอททั้งหมด.

**เกณฑ์ผ่าน:** ผู้ใช้รับเฉพาะการแจ้งเตือนของ coaster ที่ตนมีสิทธิ์และเลือก
เปิดรับ; revoke LINE link แล้วไม่มีการส่งครั้งต่อไป.

### ระยะ 7 — ย้ายข้อมูล, rollout และ hardening

- สำรอง RTDB ก่อนเปลี่ยน schema และทดสอบ migration บนสำเนาข้อมูล.
- ข้อมูลเก่าที่ `/readings`/`latest` ไม่มี owner/coaster ID ห้ามเดาเจ้าของ;
  ย้ายได้เฉพาะเมื่อยืนยันได้ว่ามาจาก coaster ใดและเจ้าของใด มิฉะนั้นเก็บเป็น
  legacy dataset แยกหรือไม่ย้าย.
- เปลี่ยนจาก test-mode rules เป็น rules ใหม่หลัง emulator tests ผ่าน.
- เปิด certificate verification แทน `setInsecure()` ทั้งการเชื่อม Firebase และ
  LINE ตาม trust chain ที่รองรับ.
- เพิ่ม monitoring สำหรับ device auth failure, pairing attempts, rules denied,
  ส่งข้อมูลซ้ำและการส่งแจ้งเตือนล้มเหลว.

**เกณฑ์ผ่าน:** ไม่มี rules เปิดอ่าน/เขียนใน production, ไม่มี credentials จริง
ใน Git/firmware, rollback ได้ และตรวจสอบการเข้าถึงข้ามผู้ใช้ไม่ผ่าน.

## แผนทดสอบขั้นต่ำ

| กรณี | ผลที่ต้องได้ |
|---|---|
| ไม่ login เปิด path ของ coaster | ถูกปฏิเสธ |
| User A อ่าน coaster ที่ A เป็น owner | อนุญาต |
| User A อ่าน coaster ของ User B | ถูกปฏิเสธ |
| User A แก้ `ownerUid` หรือ `gatewayId` ด้วย client | ถูกปฏิเสธ |
| Gateway A เขียนข้อมูลใน coaster A | อนุญาต |
| Gateway A เปลี่ยน path ไปเขียน coaster B | ถูกปฏิเสธ |
| ใช้ pairing code ซ้ำ/หมดอายุ | ถูกปฏิเสธและมี audit |
| ยกเลิกหรือโอน coaster | สิทธิ์เดิมถูกถอน และสิทธิ์ใหม่สอดคล้องทั้ง metadata/index |
| user มี coaster สองเครื่อง | dashboard แสดงและเลือกได้ทั้งสองเครื่อง |
| LINE mapping ถูก revoke/ปิด notification | backend ไม่ส่งข้อความให้ผู้ใช้นั้น |

ให้ทดสอบกรณีเหล่านี้ด้วย Firebase Emulator ก่อน deploy rules; อย่าทดสอบ
permission โดยเปิด database rules เป็น public บนฐานข้อมูลที่มีข้อมูลจริง.

## จุดตัดสินใจก่อนเริ่มลงมือ

1. ผู้ใช้หนึ่งคนเป็น owner คนเดียว หรืออนุญาตให้แชร์ coaster กับหลายบัญชีตั้งแต่
   MVP?
2. ต้องการให้ Gateway เขียน Firebase โดยตรงด้วย device identity หรือให้ส่งผ่าน
   backend?
3. จะสร้าง pairing code/QR ตอนผลิต, ตอน provision โดยผู้ดูแล, หรือใช้ปุ่ม
   physical pairing?
4. ต้องการ LINE ส่วนตัวต่อผู้ใช้หรือไม่ และจะใช้ LINE Login/LIFF เพื่อจับคู่
   LINE account หรือไม่?
5. ข้อมูลดิบและประวัติการดื่มเก็บนานเท่าไร และจัดการเมื่อยกเลิกบัญชี/ขายต่อ
   อย่างไร?

## ไฟล์ปัจจุบันที่เกี่ยวข้อง

- `gateway/gateway.ino` — เขียน readings ปัจจุบันไป root `/readings` และ
  `/latest`; ต้องเพิ่มการระบุ coaster และ authentication ต่ออุปกรณ์.
- `common/msg.h`, `node/msg.h`, `gateway/msg.h` — packet ESP-NOW ปัจจุบันมี
  `node_id` แต่ไม่มี global coaster identity.
- `firebase/database.rules.json` — rules ปัจจุบันยังไม่แยก owner ของข้อมูล.
- `firebase/database.rules.test-mode.json` — rules เปิดทั้งหมดสำหรับทดสอบ
  เท่านั้น.
- `dashboard/index.html` — ปัจจุบันอ่าน root `/latest` และ `/readings` และยัง
  ไม่มีหน้า login.
- `dashboard/config.example.js` — Firebase Web app configuration; config นี้
  ไม่ได้แทนการ login หรือ database authorization.

แผนนี้ไม่เปลี่ยน Firebase, dashboard หรือ firmware ในทันที แต่กำหนดลำดับและ
เกณฑ์ตรวจรับก่อนเริ่ม implementation เพื่อไม่ให้การเพิ่ม user login กลายเป็น
เพียงการซ่อนข้อมูลในหน้าเว็บโดยที่ database ยังอ่านข้ามผู้ใช้ได้.
