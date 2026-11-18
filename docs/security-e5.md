# E5: ตรวจ certificate + Firebase Auth

โค้ดทำให้แล้วทั้งหมด เหลือ **ตั้งค่าใน Firebase console เอง 5 ขั้น** (~15 นาที)
ทำหลังถ่ายวิดีโอ checkpoint ได้ ระหว่างนี้ถ้า `FB_EMAIL` ว่าง gateway ก็ยังส่งข้อมูลได้แบบ test mode เหมือนเดิม

## สิ่งที่โค้ดทำ

| เดิม | ตอนนี้ |
|---|---|
| `client.setInsecure()` ไม่ตรวจว่าคุยกับเซิร์ฟเวอร์จริงไหม | `client.setCACert(ROOT_CA_BUNDLE)` ตรวจ cert กับ root CA ใน `gateway/root_ca.h` (GTS Root R1/R4 ของ Google, DigiCert Global Root G2 ของ LINE) |
| rules test mode ใครก็อ่าน/เขียนได้ | gateway ล็อกอินด้วยอีเมล/รหัสผ่าน ได้ `idToken` แนบทุกคำขอ, rules ยอมให้เขียนเฉพาะ uid ของ gateway |
| `DB_AUTH` ต้องใส่ token เอง (หมดอายุใน 1 ชม.) | `gateway/firebase_auth.h` ล็อกอินเองและต่ออายุ token อัตโนมัติก่อนหมด 5 นาที |
| dashboard เปิดอ่านได้ทุกคน | dashboard มีฟอร์มล็อกอิน (ขึ้นเองเมื่อ rules ไม่ให้อ่าน) |

## ขั้นตอนที่ต้องทำเอง

### 1. เปิดการล็อกอินแบบอีเมล
Firebase console → **Build → Authentication** → Get started → แท็บ **Sign-in method** → **Email/Password** → Enable → Save

### 2. สร้าง 2 บัญชี
แท็บ **Users** → **Add user**
- บัญชีของ gateway เช่น `gateway@smartcoaster.local` + รหัสผ่านยาว ๆ (ไม่ต้องเป็นอีเมลจริง)
- บัญชีของคนดู dashboard เช่น อีเมลตัวเอง

จด **User UID** ของบัญชี gateway (คอลัมน์ User UID ในตาราง Users)

### 3. ใส่ค่าใน `gateway/secrets.h`
คัดลอกบรรทัดใหม่จาก `gateway/secrets.example.h` มาใส่:
```c
#define FB_API_KEY  "AIza..."                     // Project settings (รูปเฟือง) → General → Web API Key
#define FB_EMAIL    "gateway@smartcoaster.local"
#define FB_PASSWORD "รหัสผ่านที่ตั้งในขั้น 2"
#define USE_ROOT_CA 1
```
บรรทัด `DB_AUTH` เดิมลบทิ้งได้ อัปโหลด gateway แล้วดู Serial ต้องเห็น
```
[AUTH] signed in  uid=AbC123...
```
(uid ต้องตรงกับที่จดในขั้น 2)

### 4. เปลี่ยน rules
เปิด `firebase/database.rules.json` แก้ `REPLACE_WITH_GATEWAY_UID` เป็น UID ของ gateway
แล้วคัดลอกทั้งไฟล์ไปวางที่ Firebase console → **Realtime Database → Rules** → **Publish**
(หรือใช้ `firebase deploy --only database` ถ้าลง firebase-tools แล้ว)

หลัง Publish:
- Serial ของ gateway ยังต้องขึ้นส่งสำเร็จ (ไม่มี `[CLOUD] PATCH / -> 401`)
- dashboard จะขึ้นฟอร์มล็อกอิน → ใช้บัญชีคนดูจากขั้น 2

### 5. ทดสอบเก็บหลักฐานใส่ report
1. **cert:** อัปโหลด `tests/t12_tls` (คัดลอก `secrets.example.h` เป็น `secrets.h` ก่อน)
   - ชุด [1] ต้อง `OK` ครบ 4 โฮสต์
   - ชุด [2] ต้อง `FAIL` = ESP32 ไม่ยอมคุยกับเซิร์ฟเวอร์ที่ cert ไม่ตรง root ที่เชื่อถือ (กัน man-in-the-middle)
2. **rules:** เปิดเบราว์เซอร์ไปที่ `https://<DB_URL>/latest.json` โดยไม่ล็อกอิน ต้องได้ `"Permission denied"`
3. **dashboard:** ออกจากระบบแล้วต้องไม่เห็นข้อมูล

## ถ้าเจอปัญหา

| อาการใน Serial | สาเหตุ / วิธีแก้ |
|---|---|
| `[AUTH] sign-in FAILED 400 INVALID_LOGIN_CREDENTIALS` | อีเมล/รหัสผ่านใน secrets.h ไม่ตรงกับขั้น 2 |
| `[AUTH] sign-in FAILED 400 API key not valid` | `FB_API_KEY` ผิด |
| `[AUTH] sign-in FAILED 400 OPERATION_NOT_ALLOWED` | ยังไม่ได้ Enable Email/Password (ขั้น 1) |
| `[CLOUD] PATCH / -> 401` | UID ใน rules ไม่ตรงกับ uid ที่ gateway พิมพ์ |
| `-1` / `-3` ทุกคำขอ + t12 บางโฮสต์ `FAIL` | root CA ไม่ครบ ดูหัวข้อถัดไป ระหว่างหาสาเหตุตั้ง `USE_ROOT_CA 0` ชั่วคราวได้ |

ระหว่างแก้ปัญหา ข้อมูลไม่หาย: ส่งไม่สำเร็จจะเก็บลง flash (E4) แล้วส่งใหม่เองเมื่อแก้เสร็จ

### เพิ่ม root CA เมื่อโฮสต์ไหน FAIL
บริษัทอาจเปลี่ยนผู้ออก cert ได้ (ไม่บ่อย) วิธีหา root ตัวใหม่:
1. เปิดโฮสต์นั้นใน Chrome เช่น `https://api.line.me` → คลิกไอคอนหน้า URL → Connection is secure → Certificate is valid
2. แท็บ **Details** → เลือกตัวบนสุดของ Certificate Hierarchy (root) → **Export** แบบ Base64 (PEM)
3. เปิดไฟล์ด้วย Notepad คัดลอกทุกบรรทัดมาต่อท้าย `ROOT_CA_BUNDLE` ใน `gateway/root_ca.h` ในรูปแบบเดียวกัน (`"...\n"` ทีละบรรทัด)
4. คัดลอก `gateway/root_ca.h` ไปทับ `tests/t12_tls/root_ca.h` แล้วรัน t12 ใหม่

> ยังไม่ได้ทดสอบบนบอร์ดจริง: ชื่อ root ของ LINE (DigiCert Global Root G2) ตรวจจากเครื่องที่เขียนโค้ดไม่ได้ ใส่ USERTrust RSA สำรองไว้ด้วย ให้ยืนยันด้วย t12
