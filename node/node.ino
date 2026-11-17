// node/node.ino
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HX711.h>
#include "msg.h"

// ===== แก้ 3 ค่านี้ =====
uint8_t GATEWAY_MAC[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};  // MAC จาก test blink ของ GATEWAY
const uint8_t WIFI_CHANNEL = 6;                                  // ช่องที่ gateway พิมพ์ออกมา
const float CAL_FACTOR = 426.76f;                                // จาก 2.6
// ========================

// ===== โหมดน้ำหนักจำลอง (ระหว่างรอเปลี่ยน load cell / HX711) =====
// 1 = ไม่แตะ HX711 เลย ใช้น้ำหนักจำลองแทน (HC-SR04 ยังอ่านค่าจริง)
// 0 = ใช้ load cell จริง (เปลี่ยนกลับเป็น 0 เมื่อได้ของใหม่และใส่ CAL_FACTOR แล้ว)
// PlatformIO: ใส่ -DMOCK_WEIGHT=0 ใน build_flags ของ env:node ก็ได้
#ifndef MOCK_WEIGHT
#define MOCK_WEIGHT 1
#endif
const float    MOCK_FULL_G  = 350.0f;          // แก้วเต็ม (แก้ว + น้ำ)
const float    MOCK_EMPTY_G = 120.0f;          // แก้วเปล่า ต่ำกว่านี้ = เติมน้ำใหม่อัตโนมัติ
const float    MOCK_SIP_G   = 25.0f;           // จิบหนึ่งครั้ง
const uint32_t MOCK_SIP_EVERY_MS = 60000;      // จิบอัตโนมัติทุก 1 นาที (0 = ปิด ใช้คำสั่ง Serial อย่างเดียว)
// ===================================================================

const uint8_t NODE_ID = 1;
const int HX_DT = 19, HX_SCK = 18, TRIG = 25, ECHO = 26;
const uint32_t SEND_EVERY_MS = 5000;

#if !MOCK_WEIGHT
HX711 scale;
#endif
NodeMsg msg;
uint32_t seq = 0, okCount = 0, failCount = 0;

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS) okCount++; else failCount++;
}

float readCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);
  return us == 0 ? -1 : us * 0.0343f / 2.0f;
}

#if MOCK_WEIGHT
// น้ำหนักจำลอง: เริ่มแก้วเต็ม จิบลดทีละ MOCK_SIP_G ทุก MOCK_SIP_EVERY_MS
// คำสั่งทาง Serial Monitor (ส่งทีละบรรทัด):
//   d = จิบ 1 ครั้ง   r = เติมน้ำเต็มแก้ว   l = ยกแก้วออก (0 g)
//   p = วางแก้วกลับ   w<เลข> = ตั้งน้ำหนักเอง เช่น w280
float mockWeight = MOCK_FULL_G;   // น้ำหนักแก้วตอนวางอยู่
bool  mockLifted = false;         // true = ยกแก้วออก แท่นอ่านได้ 0 g
uint32_t mockLastSip = 0;

void mockSip() {
  mockWeight -= MOCK_SIP_G;
  if (mockWeight < MOCK_EMPTY_G) mockWeight = MOCK_FULL_G;   // หมดแก้ว → เติมใหม่
  Serial.printf("[MOCK] sip -> %.0f g\n", mockWeight);
}

void mockHandleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') { line += c; continue; }
    line.trim();
    if (line == "d")      { mockSip(); }
    else if (line == "r") { mockWeight = MOCK_FULL_G; Serial.println("[MOCK] refill"); }
    else if (line == "l") { mockLifted = true;  Serial.println("[MOCK] cup lifted"); }
    else if (line == "p") { mockLifted = false; Serial.println("[MOCK] cup placed"); }
    else if (line.startsWith("w") && line.length() > 1) {
      mockWeight = line.substring(1).toFloat();
      mockLifted = false;
      Serial.printf("[MOCK] weight = %.0f g\n", mockWeight);
    }
    line = "";
  }
}

float readWeight() {
  if (MOCK_SIP_EVERY_MS && !mockLifted && millis() - mockLastSip >= MOCK_SIP_EVERY_MS) {
    mockLastSip = millis();
    mockSip();
  }
  if (mockLifted) return 0.0f;
  return mockWeight + random(-20, 21) / 10.0f;   // ใส่ noise ±2 g ให้ดูเหมือนค่าจริง
}
#else
float readWeight() {
  if (!scale.wait_ready_timeout(500)) return NAN;  // HX711 ไม่ตอบ ส่ง NaN แทนการค้าง
  return scale.get_units(5);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
#if MOCK_WEIGHT
  randomSeed(esp_random());
  mockLastSip = millis();
  Serial.println("*** MOCK_WEIGHT = 1: ไม่ใช้ HX711, น้ำหนักเป็นค่าจำลอง ***");
  Serial.println("Serial: d=จิบ r=เติม l=ยกแก้ว p=วางแก้ว w280=ตั้งน้ำหนัก");
#else
  scale.begin(HX_DT, HX_SCK);
  scale.set_scale(CAL_FACTOR);
  delay(2000);
  if (scale.wait_ready_timeout(2000)) scale.tare(20);
  else Serial.println("HX711 not responding (เช็คสาย DT/SCK/VCC) -> weight = NaN");
#endif

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); while (true) delay(1000); }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, GATEWAY_MAC, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("add peer FAILED");
  Serial.printf("Node %d ready on channel %d\n", NODE_ID, WIFI_CHANNEL);
}

void loop() {
#if MOCK_WEIGHT
  mockHandleSerial();
#endif
  static uint32_t last = 0;
  if (millis() - last < SEND_EVERY_MS) return;
  last = millis();

  msg.version  = MSG_VERSION;
  msg.node_id  = NODE_ID;
  msg.seq      = ++seq;
  msg.weight_g = readWeight();
  msg.dist_cm  = readCm();
  msg.batt_mv  = 0;

  esp_err_t r = esp_now_send(GATEWAY_MAC, (uint8_t *)&msg, sizeof(msg));
  Serial.printf("%sseq=%lu  w=%.1f g  d=%.1f cm  send=%s  (ok=%lu fail=%lu)\n",
                MOCK_WEIGHT ? "[MOCK] " : "", msg.seq, msg.weight_g, msg.dist_cm, r == ESP_OK ? "queued" : "ERR",
                okCount, failCount);
}
