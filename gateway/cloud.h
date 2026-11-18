// gateway/cloud.h — ส่งข้อมูลขึ้น Firebase RTDB + คิวเก็บลง flash ตอนเน็ตล่ม (E4)
// ใช้จาก netTask เท่านั้น (task เดียว ไม่ต้องล็อก)
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <sys/time.h>
#include "secrets.h"
#include "root_ca.h"
#include "firebase_auth.h"

#ifndef DB_AUTH
#define DB_AUTH ""        // แบบเก่า (database secret) ไม่แนะนำ ใช้ FB_EMAIL/FB_PASSWORD แทน
#endif

// ---------------- เวลา (NTP) ----------------
inline bool timeValid() { return time(nullptr) > 1700000000; }   // ได้เวลาจริงจาก NTP แล้ว
inline uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

// ---------------- รายการรออัปโหลด ----------------
struct UpItem {
  char path[12];    // "readings" / "drinks" / "refills"
  char key[32];     // key เรียงตามเวลา เช่น 1763456789012-1-42  ("" = ให้ Firebase สร้างเอง)
  bool latest;      // เขียนทับ /latest ด้วย (เฉพาะค่าสด ไม่ใช่ค่าที่ค้างใน flash)
  char body[600];   // JSON
};

// ---------------- HTTPS ----------------
WiFiClientSecure fbClient;
HTTPClient fbHttp;
uint32_t fbOk = 0, fbFail = 0;

void cloudBegin() {
  secureClient(fbClient);   // E5: ตรวจ cert ด้วย root CA (root_ca.h)
#ifndef SC_ROOT_CA
  Serial.println("[CLOUD] ⚠️ USE_ROOT_CA = 0 → ไม่ตรวจ certificate");
#endif
  if (!fbAuthEnabled()) Serial.println("[CLOUD] ⚠️ ไม่ได้ตั้ง FB_EMAIL → ไม่ล็อกอิน (ใช้ได้กับ rules test mode เท่านั้น)");
  fbHttp.setReuse(true);    // keep-alive: ไม่ต้อง handshake TLS ใหม่ทุกครั้ง เร็วขึ้นมาก
  fbHttp.setConnectTimeout(8000);
  fbHttp.setTimeout(8000);
}

int fbRequest(const char *method, const String &path, const String &body) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  String auth;
  if (fbAuthEnabled()) {
    String tok = fbToken();
    if (!tok.length()) return -3;          // ล็อกอินไม่ได้ (รหัสผิด / API key ผิด) เก็บลง flash ไว้ก่อน
    auth = "?auth=" + tok;
  } else if (strlen(DB_AUTH)) {
    auth = String("?auth=") + DB_AUTH;
  }
  if (!fbHttp.begin(fbClient, String(DB_URL) + path + ".json" + auth)) return -2;
  fbHttp.addHeader("Content-Type", "application/json");
  int code = fbHttp.sendRequest(method, body);
  fbHttp.end();
  if (code == 401 && fbAuthEnabled()) fbTokenRejected();
  if (code != 200) {
    fbClient.stop();        // เริ่ม connection ใหม่รอบหน้า
    fbFail++;
    Serial.printf("[CLOUD] %s %s -> %d\n", method, path.c_str(), code);
  } else {
    fbOk++;
  }
  return code;
}

// อัปโหลด 1 รายการ: ถ้ามี key ใช้ PATCH ครั้งเดียวเขียนทั้ง /readings/<key> และ /latest
bool fbUpload(const UpItem &it) {
  if (it.key[0]) {
    String b = String("{\"") + it.path + "/" + it.key + "\":" + it.body;
    if (it.latest) b += String(",\"latest\":") + it.body;
    b += "}";
    return fbRequest("PATCH", "/", b) == 200;
  }
  int code = fbRequest("POST", String("/") + it.path, it.body);
  if (code == 200 && it.latest) fbRequest("PUT", "/latest", it.body);
  return code == 200;
}

// ---------------- คิวบน flash (LittleFS) ----------------
// ไฟล์ /queue.txt  บรรทัดละรายการ: path<TAB>key<TAB>json
// อยู่รอดแม้ gateway ดับ/รีเซ็ต พอเน็ตกลับมา netTask ค่อย ๆ ไล่ส่ง (เก่าสุดก่อน)
const char *QFILE = "/queue.txt";
const char *QTMP  = "/queue.tmp";
const size_t QFILE_MAX = 300 * 1024;    // ~1,000 รายการ (~1.5 ชม. ที่ 5 วิ/ค่า)
bool fsOk = false;
uint32_t storeCount = 0, storeDropped = 0;

void storeBegin() {
  fsOk = LittleFS.begin(true);          // true = format ถ้ายังไม่เคยใช้
  if (!fsOk) { Serial.println("[STORE] LittleFS mount FAILED → ไม่มีคิว offline"); return; }
  File f = LittleFS.open(QFILE, FILE_READ);
  storeCount = 0;
  if (f) { while (f.available()) { f.readStringUntil('\n'); storeCount++; } f.close(); }
  Serial.printf("[STORE] ค้างใน flash %lu รายการ\n", (unsigned long)storeCount);
}

bool storeAppend(const UpItem &it) {
  if (!fsOk) return false;
  File f = LittleFS.open(QFILE, FILE_APPEND);
  if (!f) return false;
  if (f.size() > QFILE_MAX) { f.close(); storeDropped++; return false; }   // เต็ม ทิ้งค่าใหม่
  f.print(String(it.path) + "\t" + it.key + "\t" + it.body + "\n");
  f.close();
  storeCount++;
  return true;
}

// ส่งที่ค้างใน flash ได้สูงสุด maxItems รายการ คืนจำนวนที่ส่งสำเร็จ
int storeFlush(int maxItems) {
  if (!fsOk || storeCount == 0) return 0;
  File in = LittleFS.open(QFILE, FILE_READ);
  if (!in) { storeCount = 0; return 0; }
  int sent = 0;
  bool failed = false;
  File out;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (!failed && sent < maxItems) {
      int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1);
      if (t1 > 0 && t2 > t1) {
        UpItem it = {};
        strlcpy(it.path, line.substring(0, t1).c_str(), sizeof(it.path));
        strlcpy(it.key,  line.substring(t1 + 1, t2).c_str(), sizeof(it.key));
        strlcpy(it.body, line.substring(t2 + 1).c_str(), sizeof(it.body));
        it.latest = false;
        if (fbUpload(it)) { sent++; continue; }
        if (sent == 0) { in.close(); return 0; }   // ยังส่งไม่ได้เลย ไม่ต้องเขียนไฟล์ใหม่
        failed = true;
      } else {
        continue;                          // บรรทัดเสีย ทิ้ง
      }
    }
    if (!out) out = LittleFS.open(QTMP, FILE_WRITE);
    out.print(line + "\n");                // ยังไม่ได้ส่ง เก็บไว้
  }
  in.close();
  LittleFS.remove(QFILE);
  if (out) { out.close(); LittleFS.rename(QTMP, QFILE); }
  storeCount = storeCount > (uint32_t)sent ? storeCount - sent : 0;
  if (!LittleFS.exists(QFILE)) storeCount = 0;
  if (sent) Serial.printf("[STORE] ส่งค้างสำเร็จ %d รายการ เหลือ %lu\n", sent, (unsigned long)storeCount);
  return sent;
}
