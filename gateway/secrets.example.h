// gateway/secrets.example.h
// คัดลอกไฟล์นี้เป็น secrets.h (อยู่โฟลเดอร์เดียวกัน) แล้วใส่ค่าจริง
// secrets.h อยู่ใน .gitignore แล้ว ห้าม commit
#pragma once
#define WIFI_SSID   "your-hotspot-name"      // hotspot ต้องเป็น 2.4 GHz
#define WIFI_PASS   "your-hotspot-password"
#define DB_URL      "https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app"  // ไม่มี / ท้าย
#define DB_AUTH     ""     // ว่างไว้ตอน test mode, ใส่ idToken ในขั้น 5.5 (E5)
#define LINE_TOKEN  "paste-channel-access-token-long-lived"
