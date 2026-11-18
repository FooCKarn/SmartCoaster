// node/node.ino
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HX711.h>
#include "msg.h"

// ===== แก้ 3 ค่านี้ =====
uint8_t GATEWAY_MAC[6] = {0x88, 0x13, 0xBF, 0x0D, 0x25, 0x98};  // MAC จาก test blink ของ GATEWAY
const uint8_t WIFI_CHANNEL = 4;                                  // ช่องที่ gateway พิมพ์ออกมา
const float CAL_FACTOR = 1185.23f;                                // จาก 2.6
// ========================

// ===== โหมดน้ำหนักจำลอง =====
// 0 = ใช้ load cell จริง (ค่าเริ่มต้น ตอนนี้ load cell ใช้ได้แล้ว)
// 1 = ไม่แตะ HX711 เลย ใช้น้ำหนักจำลองแทน (HC-SR04 ยังอ่านค่าจริง) ไว้เทสเมื่อ load cell มีปัญหา
// PlatformIO: ใส่ -DMOCK_WEIGHT=1 ใน build_flags ของ env:node ก็ได้
#ifndef MOCK_WEIGHT
#define MOCK_WEIGHT 0
#endif
const float    MOCK_FULL_G  = 350.0f;          // แก้วเต็ม (แก้ว + น้ำ)
const float    MOCK_EMPTY_G = 120.0f;          // แก้วเปล่า ต่ำกว่านี้ = เติมน้ำใหม่อัตโนมัติ
const float    MOCK_SIP_G   = 25.0f;           // จิบหนึ่งครั้ง
const uint32_t MOCK_SIP_EVERY_MS = 60000;      // จิบอัตโนมัติทุก 1 นาที (0 = ปิด ใช้คำสั่ง Serial อย่างเดียว)
// ===================================================================

const uint8_t NODE_ID = 1;
const int HX_DT = 19, HX_SCK = 18, TRIG = 25, ECHO = 26;
const uint32_t SEND_EVERY_MS = 5000;

// ===== E4: ACK + คิวส่งซ้ำ =====
// ทุกค่าที่วัดเข้าคิวก่อน แล้วส่งจากหัวคิว (เก่าสุดก่อน) รอ AckMsg จาก gateway
// ไม่ได้ ACK ภายใน ACK_TIMEOUT_MS = เก็บไว้ส่งรอบหน้า (gateway ดับ/อยู่ไกล ข้อมูลไม่หาย)
// คิวเต็ม = ทิ้งค่าที่เก่าที่สุด
const uint32_t ACK_TIMEOUT_MS = 250;
const int      QUEUE_MAX      = 60;     // 60 ค่า × 5 วิ = เก็บได้ 5 นาที
const int      SEND_PER_ROUND = 6;      // รอบหนึ่งส่งค้างได้สูงสุดกี่ค่า (ไล่คิวทัน)

struct Pending { NodeMsg msg; uint32_t takenMs; };
Pending queue_[QUEUE_MAX];
int qHead = 0, qCount = 0;
uint16_t bootId;
uint32_t seq = 0;
uint32_t ackedCount = 0, retryCount = 0, droppedCount = 0, macFailCount = 0;
volatile uint32_t lastAckSeq = 0;
volatile bool ackReceived = false;
// ===============================

#if !MOCK_WEIGHT
HX711 scale;
#endif

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  if (status != ESP_NOW_SEND_SUCCESS) macFailCount++;
}

// gateway ตอบ AckMsg กลับมา
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(AckMsg)) return;
  const AckMsg *a = (const AckMsg *)data;
  if (a->version != MSG_VERSION || a->type != MSG_ACK || a->node_id != NODE_ID || a->boot_id != bootId) return;
  lastAckSeq = a->seq;
  ackReceived = true;
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
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, GATEWAY_MAC, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("add peer FAILED");
  bootId = (uint16_t)esp_random();
  Serial.printf("Node %d ready on channel %d (boot_id=%u)\n", NODE_ID, WIFI_CHANNEL, bootId);
}

// ---------- คิว ----------
void enqueue(const NodeMsg &m) {
  if (qCount == QUEUE_MAX) {                 // เต็ม → ทิ้งเก่าสุด
    qHead = (qHead + 1) % QUEUE_MAX;
    qCount--;
    droppedCount++;
  }
  Pending &p = queue_[(qHead + qCount) % QUEUE_MAX];
  p.msg = m;
  p.takenMs = millis();
  qCount++;
}

// ส่งหัวคิวแล้วรอ ACK  คืน true ถ้า gateway ตอบรับ
bool sendHeadAndWaitAck() {
  Pending &p = queue_[qHead];
  p.msg.age_ms = millis() - p.takenMs;
  p.msg.queued = (uint8_t)min(qCount - 1, 255);
  ackReceived = false;
  if (esp_now_send(GATEWAY_MAC, (uint8_t *)&p.msg, sizeof(NodeMsg)) != ESP_OK) return false;
  uint32_t t0 = millis();
  while (millis() - t0 < ACK_TIMEOUT_MS) {
    if (ackReceived && lastAckSeq == p.msg.seq) return true;
    delay(2);
  }
  return false;
}

void flushQueue() {
  for (int i = 0; i < SEND_PER_ROUND && qCount > 0; i++) {
    bool resend = queue_[qHead].msg.age_ms > 0;   // เคยส่งแล้วไม่ได้ ACK
    if (!sendHeadAndWaitAck()) return;        // gateway ไม่ตอบ เก็บไว้รอบหน้า
    if (resend) retryCount++;
    ackedCount++;
    qHead = (qHead + 1) % QUEUE_MAX;
    qCount--;
  }
}

void loop() {
#if MOCK_WEIGHT
  mockHandleSerial();
#endif
  static uint32_t last = 0;
  if (millis() - last < SEND_EVERY_MS) return;
  last = millis();

  NodeMsg msg = {};
  msg.version  = MSG_VERSION;
  msg.type     = MSG_DATA;
  msg.node_id  = NODE_ID;
  msg.boot_id  = bootId;
  msg.seq      = ++seq;
  msg.weight_g = readWeight();
  msg.dist_cm  = readCm();
  msg.batt_mv  = 0;
  enqueue(msg);
  flushQueue();

  Serial.printf("%sseq=%lu  w=%.1f g  d=%.1f cm  | acked=%lu resent=%lu queued=%d dropped=%lu macFail=%lu\n",
                MOCK_WEIGHT ? "[MOCK] " : "", msg.seq, msg.weight_g, msg.dist_cm,
                ackedCount, retryCount, qCount, droppedCount, macFailCount);
}
