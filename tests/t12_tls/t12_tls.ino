// tests/t12_tls — ทดสอบการตรวจ certificate (E5) บน gateway
// ต่อ Wi-Fi แล้วลองเปิด TLS ไปทุกโฮสต์ที่ระบบใช้ ด้วย root CA ใน root_ca.h
//   OK   = cert ถูกต้อง เชื่อมได้
//   FAIL = root CA ไม่ครบ (ดู docs/security-e5.md วิธีเพิ่ม root)
// ท้ายสุดลองต่อ Firebase ด้วย root ที่ "ผิด" ตั้งใจให้ FAIL → พิสูจน์ว่าตรวจ cert จริง (ใส่ใน report ได้)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "secrets.h"
#include "root_ca.h"

// root ที่ไม่เกี่ยวกับ Google (ISRG Root X1 ของ Let's Encrypt) ใช้ทดสอบว่าต้อง FAIL
static const char WRONG_ROOT[] =
  "-----BEGIN CERTIFICATE-----\n"
  "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\n"
  "-----END CERTIFICATE-----\n";

const char *HOSTS[] = {DB_HOST, "identitytoolkit.googleapis.com", "securetoken.googleapis.com", "api.line.me"};

bool tryHost(const char *host, const char *ca) {
  WiFiClientSecure c;
  c.setCACert(ca);
  c.setTimeout(10);
  bool ok = c.connect(host, 443);
  char err[100] = "";
  if (!ok) c.lastError(err, sizeof(err));
  Serial.printf("  %-60s %s %s\n", host, ok ? "OK" : "FAIL", err);
  c.stop();
  return ok;
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  configTzTime("ICT-7", "pool.ntp.org");
  struct tm t; getLocalTime(&t, 10000);   // บางเวอร์ชันตรวจวันหมดอายุ cert ต้องมีเวลาจริงก่อน
  Serial.printf("\nWiFi OK, time %04d-%02d-%02d\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

  Serial.println("\n[1] ใช้ root_ca.h (ต้อง OK ทุกบรรทัด)");
  int ok = 0;
  for (const char *h : HOSTS) ok += tryHost(h, ROOT_CA_BUNDLE);
  Serial.printf("ผ่าน %d/%d\n", ok, (int)(sizeof(HOSTS) / sizeof(HOSTS[0])));

  Serial.println("\n[2] ใช้ root ผิด (ต้อง FAIL = ESP32 ตรวจ cert จริง ไม่ยอมคุยกับ cert ที่ไม่น่าเชื่อถือ)");
  tryHost(DB_HOST, WRONG_ROOT);
}

void loop() {}
