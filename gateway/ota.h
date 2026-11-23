// gateway/ota.h — E6: อัปเดต firmware ผ่าน WiFi (ArduinoOTA + รหัสผ่าน)
//
// ใช้งาน: ใส่ OTA_PASSWORD ใน secrets.h → อัปโหลดผ่าน USB ครั้งแรก 1 ครั้ง
//         ครั้งต่อไป Arduino IDE → Tools → Port → "smartcoaster-gw at 192.168.x.x" → Upload
// ขั้นตอนละเอียด: docs/ota-e6.md
//
// ทำงานใน otaTask ของตัวเอง (FreeRTOS) ไม่ยุ่งกับ ESP-NOW / Firebase:
//  - รอ WiFi ติดก่อนค่อย ArduinoOTA.begin() (ESP-NOW รับต่อได้ตามปกติระหว่างรอ)
//  - ระหว่างอัปโหลด ตั้ง otaActive = true → netTask พักส่ง Firebase/LINE (คืน RAM ให้ OTA, ไม่เขียน flash ซ้อน)
//    และ servoTask หยุดเซอร์โว ข้อมูลที่ node ส่งมาตอนนี้ node เก็บในคิวของตัวเองแล้วส่งซ้ำหลังรีบูต (E4)
//  - อัปโหลดเสร็จ บอร์ดรีบูตเองแล้วรันโค้ดใหม่
#pragma once
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "smartcoaster-gw"
#endif

#ifndef OTA_PASSWORD
#warning "ยังไม่มี OTA_PASSWORD ใน secrets.h → ปิด OTA (ดู secrets.example.h)"
#define OTA_PASSWORD ""
#endif

volatile bool otaActive = false;   // true ระหว่างกำลังรับ firmware ใหม่

static void otaSetup() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);   // IDE จะถามรหัสนี้ตอนอัปโหลด
  ArduinoOTA.setRebootOnSuccess(true);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    Serial.printf("[OTA] เริ่มรับ firmware ใหม่ (%s) — พักส่ง Firebase/LINE และหยุดเซอร์โว\n",
                  ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int lastPct = -1;
    int pct = total ? (int)(done * 100UL / total) : 0;
    if (pct / 10 != lastPct / 10) { lastPct = pct; Serial.printf("[OTA] %d%%\n", pct); }
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] เสร็จ 100% → รีบูตเข้าโค้ดใหม่");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    const char *why = e == OTA_AUTH_ERROR    ? "รหัสผ่านผิด" :
                      e == OTA_BEGIN_ERROR   ? "เริ่มไม่ได้ (เช็ค Partition Scheme ต้องมี OTA)" :
                      e == OTA_CONNECT_ERROR ? "ต่อกับคอมไม่ได้" :
                      e == OTA_RECEIVE_ERROR ? "รับข้อมูลขาด (WiFi ไม่นิ่ง)" :
                      e == OTA_END_ERROR     ? "ตรวจ firmware ไม่ผ่าน" : "ไม่ทราบสาเหตุ";
    Serial.printf("[OTA] ล้มเหลว: %s (code %d) — ทำงานต่อด้วยโค้ดเดิม\n", why, (int)e);
    otaActive = false;   // กลับมาทำงานตามปกติ
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] พร้อม: เลือก Port \"%s at %s\" ใน Arduino IDE\n",
                OTA_HOSTNAME, WiFi.localIP().toString().c_str());
}

void otaTask(void *) {
  if (strlen(OTA_PASSWORD) == 0) {
    Serial.println("[OTA] ปิดอยู่: ใส่ #define OTA_PASSWORD \"...\" ใน secrets.h แล้วอัปโหลดผ่าน USB อีกครั้ง");
    vTaskDelete(nullptr);
  }
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(1000));   // รอ WiFi
  otaSetup();
  for (;;) {
    ArduinoOTA.handle();
    vTaskDelay(pdMS_TO_TICKS(otaActive ? 1 : 20));
  }
}
