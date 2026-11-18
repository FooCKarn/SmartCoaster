// gateway/secrets.example.h
// คัดลอกไฟล์นี้เป็น secrets.h (อยู่โฟลเดอร์เดียวกัน) แล้วใส่ค่าจริง
// secrets.h อยู่ใน .gitignore แล้ว ห้าม commit
#pragma once
#define WIFI_SSID   "your-hotspot-name"      // hotspot ต้องเป็น 2.4 GHz
#define WIFI_PASS   "your-hotspot-password"
#define DB_URL      "https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app"  // ไม่มี / ท้าย
#define LINE_TOKEN  "paste-channel-access-token-long-lived"

// ---- E5: Firebase Auth (ดู docs/security-e5.md) ----
// ว่างไว้ = ไม่ล็อกอิน ใช้ได้เฉพาะตอน rules เป็น test mode
#define FB_API_KEY  ""   // Project settings → General → Web API Key
#define FB_EMAIL    ""   // บัญชีของ gateway ที่สร้างใน Authentication → Users
#define FB_PASSWORD ""

// ---- E5: ตรวจ certificate ----
#define USE_ROOT_CA 1    // 1 = ตรวจ cert ด้วย gateway/root_ca.h, 0 = setInsecure() (แก้ปัญหาเท่านั้น)
