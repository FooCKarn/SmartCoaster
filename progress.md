# ความคืบหน้า Smart Coaster

อัปเดตล่าสุด: 19 พ.ย. 2026

| กำหนดส่ง | วันที่ | สถานะ |
|---|---|---|
| Proposal | 30 ต.ค. 2026 | ✅ ส่งแล้ว |
| วิดีโอ checkpoint | 20 พ.ย. 2026 | ⏳ พรุ่งนี้ ใช้ไฟ USB ได้ |
| Final | 8 ธ.ค. 2026 | ⏳ |

## ทำได้แล้ว
- [x] node ส่งข้อมูลถึง gateway ผ่าน ESP-NOW ได้ อ่าน load cell และ ultrasonic ได้ (ตอนเสียบ USB)
- [x] gateway ต่อ WiFi ได้ (channel 4) แก้ MAC ของ gateway ในโค้ด node แล้ว
- [x] AI บน gateway ใช้โมเดล `office_hydration_v1` ที่เทรนใหม่จากข้อมูลที่อิงงานวิจัย (PR #7) คำนวณและสั่งยก/ลดป้ายด้วยเซอร์โวได้
- [x] E4 เก็บข้อมูลตอนเน็ตหลุด ทำงานจริง (คิวบน node + คิวบน flash ของ gateway)
- [x] gateway บันทึก `line_sent` และ `retried` ส่งขึ้น dashboard
- [x] แก้ปัญหา `msg.h` หายในเครื่อง (ดาวน์โหลดไฟล์มาวางใน sketch แล้ว)
- [x] ตั้ง MT3608 ให้ออก 5 V แล้ว มี test `t13_battery_18650` และ `t14_mt3608`
- [x] วงจรทั้งระบบแบบชิ้นส่วนจริง ใช้ปุ่มกดแทนสวิตช์ มีทั้งแบบมี LDO และแบบ v2 (`docs/Circuit.png`)
- [x] เพิ่มโค้ดดีบักไฟ node ตอนใช้แบต (PR #8, merge แล้ว): ไฟสถานะ GPIO2, บันทึกสาเหตุรีเซ็ต/brownout ใน NVS, ลด TX power เป็น 11 dBm
- [x] แก้ error ตอนคอมไพล์ gateway ใน Arduino IDE (รอ Verify ซ้ำ): ลง ArduinoJson 7, DHT sensor library + Adafruit Unified Sensor, HX711 (bogde) และแก้ error `LinkStat does not name a type` (ประกาศ `struct LinkStat;` ไว้บนสุดของ `gateway.ino`)
- [x] เซอร์โวยก/ลดป้ายได้แล้ว (19 พ.ย.) ตัวที่ใช้เป็น SG90 รุ่น 360° (พัลส์คุมความเร็ว ไม่ได้คุมมุม) เลยแก้ให้หมุนตามเวลาแล้วหยุด
  - `SERVO_MODE` 0 = เซอร์โวนิ่ง 1 = รุ่น 180° สั่งเป็นมุม 2 = รุ่น 360° หมุนตามเวลา (ใช้อยู่)
  - ธงพักด้านซ้าย (หันหน้าเข้าด้านที่มีธง) `SERVO_UP_DIR = -1` หมุน `SERVO_MOVE_MS` 450 ms
  - ใส่ที่กั้นตรงจุดพักแล้ว แก้ปัญหาตำแหน่งเพี้ยนสะสมตอนยกลงหลายรอบ

## ติดอยู่
- [ ] **node ไม่ส่งข้อมูลตอนใช้แบต** เสียบ USB แล้วส่งได้ แต่ใช้แบตแล้วไม่ส่ง น่าจะเป็นเพราะไฟตกตอนส่ง
  - ต้องทำ: อัปโหลดโค้ดจาก PR #8 ลง node, ดูไฟ GPIO2 กะพริบ, อ่านสาเหตุรีเซ็ตใน Serial, และบอกเบอร์ LDO ที่ใช้
  - ไม่กระทบ checkpoint เพราะใช้ไฟ USB ได้

## ยังไม่ได้ทำ (ก่อน checkpoint 20 พ.ย.)
- [ ] ทดสอบ gateway ส่งข้อมูลขึ้น Firebase (กำลังทำ 19 พ.ย.) ยังไม่ได้ตั้ง FB_EMAIL rules ต้องเป็น test mode (ลบ / ท้าย `DB_URL` ใน `gateway/secrets.h` แล้ว)
- [ ] ทดสอบการดื่มจริงให้ขึ้น `[DRINK] ดื่ม xx ml` (ตอนนี้ยอดยังค้างที่ 0)
- [ ] Deploy dashboard แล้วเช็กว่าข้อมูลขึ้นบนเว็บ
- [ ] ถ่ายวิดีโอ checkpoint

## ก่อน final (8 ธ.ค.)
- [ ] E5 สร้าง user และตั้ง rules ใน Firebase console ตาม [docs/security-e5.md](docs/security-e5.md)
- [ ] เก็บข้อมูลจริงแล้วเทรน AI ใหม่ (`ai/train.py --firebase export.json`)
- [ ] E3 บัดกรี INA219 (ตอนนี้ขาใช้กาวร้อนติดไว้ ไฟไม่เข้า) เพื่อวัดพลังงาน
- [ ] แก้ node ให้ทำงานด้วยแบตได้ แล้วทำ deep sleep
