# E6: อัปเดต firmware gateway ผ่าน WiFi (OTA)

gateway ใช้ `ArduinoOTA` (มากับ esp32 core ไม่ต้องลง library เพิ่ม) ทำงานใน FreeRTOS task `ota`
แยกจาก task อื่น ระหว่างกำลังรับ firmware ใหม่ gateway จะพักส่ง Firebase/LINE และหยุดเซอร์โว
ส่วน ESP-NOW ยังรับต่อได้ และ node เก็บข้อมูลในคิวของตัวเองแล้วส่งซ้ำหลัง gateway รีบูต (E4)
มีรหัสผ่านกันคนอื่นอัปโค้ดเข้าบอร์ด รหัสเก็บใน `gateway/secrets.h` (ไม่ถูก commit)

node ไม่มี OTA (node ไม่ได้ต่อ WiFi ใช้ ESP-NOW อย่างเดียว)

## ครั้งแรก: อัปผ่าน USB 1 ครั้ง
1. เปิด `gateway/secrets.h` เพิ่มบรรทัด (ตั้งรหัสเอง)
   ```c
   #define OTA_PASSWORD "รหัสที่ตั้งเอง"
   ```
2. Arduino IDE → Tools → **Partition Scheme** ต้องเป็นแบบที่มี OTA เช่น
   **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)**
   (ห้ามใช้ "Huge APP (3MB No OTA)" เพราะไม่มีที่ให้ OTA ถ้าเปลี่ยน scheme คิว offline ใน flash จะถูกฟอร์แมตใหม่ 1 ครั้ง)
3. เลือก Port เป็น USB (COMx / /dev/ttyUSBx) แล้วกด Upload ตามปกติ
4. เปิด Serial Monitor ต้องเห็น
   ```
   [OTA] พร้อม: เลือก Port "smartcoaster-gw at 192.168.x.x" ใน Arduino IDE
   ```

## ครั้งต่อไป: อัปผ่าน WiFi
1. คอมต้องต่อ WiFi/hotspot **วงเดียวกับ gateway**
2. ถอดสาย USB ได้ (เสียบไฟเลี้ยงอย่างเดียวก็พอ) ปิด-เปิด Arduino IDE ถ้า port ยังไม่ขึ้น
3. Tools → Port → หัวข้อ **Network ports** → เลือก `smartcoaster-gw at 192.168.x.x`
4. กด Upload → IDE ถามรหัส ใส่ `OTA_PASSWORD` → รอจนขึ้น Done uploading บอร์ดรีบูตเองเข้าโค้ดใหม่

## ถ้าไม่เจอ network port
- hotspot มือถือบางรุ่นบล็อก mDNS → ใช้ IP ตรง ๆ แทน (ดู IP จาก Serial Monitor หรือ `[OTA] พร้อม`)
  - PlatformIO: `pio run -e gateway -t upload --upload-port 192.168.x.x` และใส่ `upload_flags = --auth=รหัส`
  - หรือใช้ `espota.py -i 192.168.x.x -a รหัส -f gateway.ino.bin` (อยู่ในโฟลเดอร์ tools ของ esp32 core)
- Windows: ถ้า Firewall เด้งถาม ให้ Allow (IDE ต้องรับการเชื่อมต่อกลับจากบอร์ด)
- Serial ขึ้น `[OTA] ล้มเหลว: รหัสผ่านผิด` → รหัสใน IDE ไม่ตรง `secrets.h` ที่อยู่ในบอร์ดตอนนี้
- `[OTA] ล้มเหลว: เริ่มไม่ได้` → Partition Scheme ไม่มี OTA กลับไปทำขั้นตอนครั้งแรกใหม่
- ขึ้น `[OTA] ปิดอยู่` → ยังไม่ได้ใส่ `OTA_PASSWORD` ใน `secrets.h`

## หลักฐานสำหรับ final
อัดคลิปสั้น: แก้ข้อความสักบรรทัด (เช่นเลขเวอร์ชันใน `Gateway ready`) → เลือก network port → Upload
→ Serial (ผ่าน USB อีกเครื่อง หรือดูจาก dashboard ที่ยังได้ข้อมูล) ขึ้น `[OTA] 10% ... 100%` แล้วรีบูตเข้าโค้ดใหม่
