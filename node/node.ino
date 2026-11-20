// node/node.ino
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HX711.h>
#include <Preferences.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "msg.h"

// ===== แก้ 3 ค่านี้ =====
uint8_t GATEWAY_MAC[6] = {0x88, 0x13, 0xBF, 0x0D, 0x25, 0x98};  // MAC จาก test blink ของ GATEWAY
const uint8_t WIFI_CHANNEL = 4;                                  // ช่องที่ gateway พิมพ์ออกมา
const float CAL_FACTOR = 1113.43f;                                // จาก 2.6
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

// ===== ดีบักตอนใช้แบต (ไม่มี Serial ให้ดู) =====
// LED บนบอร์ด (GPIO2 สีน้ำเงิน) บอกสถานะ:
//   เปิดเครื่อง        = กะพริบเร็ว 3 ครั้ง  (ถ้าเห็น 3 ครั้งซ้ำทุกไม่กี่วินาที = บอร์ดรีเซ็ตวน = ไฟตก)
//   เปิดวิทยุผ่าน       = กะพริบช้า 2 ครั้ง
//   ส่งแล้ว gateway ตอบ = กะพริบ 1 ครั้ง (0.15 วิ) ทุก 5 วิ
//   ส่งไม่ถึง/ไม่มี ACK  = ติดค้าง 1 วิ ทุก 5 วิ
// สถิติจดลง flash (NVS) แล้วเสียบ USB เปิด Serial Monitor ดูทีหลังได้ (พิมพ์ตอนบูต)
const int      STATUS_LED   = 2;
const uint32_t SAVE_EVERY_MS = 30000;
// ลดกำลังส่ง = ลดกระแสพุ่งตอนส่ง ช่วยให้ LDO จ่ายทัน วางบนโต๊ะห่าง gateway ไม่กี่เมตรพอ
// หน่วย 0.25 dBm: 34 = 8.5 dBm (ค่าเดิมของ ESP32 = 80 = 20 dBm)  ใส่ 0 = ไม่ลด
const int8_t   TX_POWER_QDBM = 34;

// ===== ลดไฟตก (brownout) ตอนใช้แบต =====
// CPU 80 MHz ยังใช้ WiFi/ESP-NOW ได้ กินไฟน้อยกว่า 240 MHz ราว 20-30 mA
const uint32_t CPU_MHZ = 80;
// รอให้ตัวเก็บประจุ/MT3608 นิ่งก่อนเปิดวิทยุ (ตอนเปิด WiFi กระแสพุ่งสูงสุด)
// ถ้ารอบก่อนรีเซ็ตเพราะไฟตก จะรอนานขึ้นเป็น RADIO_DELAY_AFTER_BROWNOUT_MS
const uint32_t RADIO_DELAY_MS = 500;
const uint32_t RADIO_DELAY_AFTER_BROWNOUT_MS = 3000;
// 1 = ปิดตัวตรวจไฟตก (ทางเลือกสุดท้าย ใช้ตอนเดโมถ้ายังรีเซ็ตวน)
//     ข้อเสีย: ถ้าไฟตกจริง บอร์ดอาจค้างหรือทำงานเพี้ยนแทนที่จะรีเซ็ต และค่าที่จดลง flash อาจเสีย
#ifndef DISABLE_BROWNOUT
#define DISABLE_BROWNOUT 0
#endif
Preferences dbg;
uint32_t noAckRounds = 0;
bool lastResetBrownout = false;

// จดว่าบูตไปถึงขั้นไหนแล้ว ถ้ารีเซ็ตกลางทาง จะรู้ว่าตายตอนไหน
// 1 = กำลังเปิด WiFi  2 = เปิด ESP-NOW ได้แล้ว  3 = ส่งรอบแรกเสร็จ
void markStage(uint8_t st) { dbg.putUChar("stage", st); }
const char *stageText(uint8_t st) {
  switch (st) {
    case 1: return "1 = ตายตอนเปิด WiFi (กระแสพุ่งตอนเริ่มวิทยุ)";
    case 2: return "2 = เปิด ESP-NOW ได้ แต่ตายก่อนส่งรอบแรกเสร็จ";
    case 3: return "3 = ส่งได้อย่างน้อย 1 รอบ";
    default: return "0 = ตายก่อนถึงขั้นเปิด WiFi";
  }
}

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
    else if (line == "c") { dbg.clear(); Serial.println("ล้างสถิติ debug แล้ว"); }
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
// ===== จุดศูนย์ (tare) จำไว้ใน flash =====
// เดิม tare ทุกครั้งที่บูต ถ้า node รีบูต (ไฟตก/ถอดปลั๊ก) ตอนแก้ววางอยู่ น้ำหนักแก้วจะกลายเป็น 0
// ยกแก้วออกได้ค่าติดลบ gateway เลยมองว่า "ไม่มีแก้ว" ตลอด และไม่เห็นการดื่ม
// ตอนนี้ tare ครั้งแรกครั้งเดียวแล้วจำ offset ไว้ บูตครั้งต่อไปใช้ค่าเดิม
// ถ้าอ่านได้ติดลบชัด ๆ (< NEG_RETARE_G) ติดกัน 3 ครั้ง = จุดศูนย์ผิด (เคย tare ตอนมีแก้ว) → tare ใหม่เอง
// tare ใหม่เองจาก Serial: พิมพ์ t (ยกแก้วออกก่อน)
Preferences scalePrefs;
const float NEG_RETARE_G = -40;
int negCount = 0;

void retare(const char *why) {
  if (!scale.wait_ready_timeout(1000)) return;
  scale.tare(20);
  scalePrefs.putLong("offset", scale.get_offset());
  negCount = 0;
  Serial.printf("[SCALE] tare ใหม่ (%s) offset=%ld บันทึกแล้ว\n", why, (long)scale.get_offset());
}

float readWeight() {
  if (!scale.wait_ready_timeout(500)) return NAN;  // HX711 ไม่ตอบ ส่ง NaN แทนการค้าง
  float w = scale.get_units(5);
  if (w < NEG_RETARE_G) {
    if (++negCount >= 3) { retare("ค่าติดลบ แท่นว่างอยู่"); w = scale.get_units(5); }
  } else {
    negCount = 0;
  }
  return w;
}
#endif

const char *resetText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (เพิ่งจ่ายไฟ)";
    case ESP_RST_BROWNOUT: return "BROWNOUT (ไฟตก!)";
    case ESP_RST_EXT:      return "EXT (ปุ่ม EN / เปิด Serial)";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC (โปรแกรมล่ม)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "WATCHDOG (ค้าง)";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    default:               return "อื่นๆ";
  }
}

void blink(int n, int onMs, int offMs) {
  for (int i = 0; i < n; i++) {
    digitalWrite(STATUS_LED, HIGH); delay(onMs);
    digitalWrite(STATUS_LED, LOW);  delay(offMs);
  }
}

// อ่านสถิติรอบก่อน (เช่นรอบที่รันจากแบต) แล้วนับรอบใหม่
void debugBoot() {
  esp_reset_reason_t reason = esp_reset_reason();
  dbg.begin("nodedbg", false);
  uint32_t boots  = dbg.getUInt("boots", 0) + 1;
  uint32_t brown  = dbg.getUInt("brown", 0) + (reason == ESP_RST_BROWNOUT ? 1 : 0);
  uint32_t panics = dbg.getUInt("panic", 0) + (reason == ESP_RST_PANIC ? 1 : 0);
  bool wdtReset = (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT);
  uint32_t wdts   = dbg.getUInt("wdt", 0) + (wdtReset ? 1 : 0);
  Serial.println();
  Serial.println("===== node debug (สถิติจาก flash) =====");
  Serial.printf("รีเซ็ตครั้งนี้เพราะ: %s\n", resetText(reason));
  Serial.printf("รอบก่อนไปถึงขั้น: %s\n", stageText(dbg.getUChar("stage", 0)));
  Serial.printf("รอบก่อน: รันได้ %lu s, acked=%lu, ส่งไม่ถึง=%lu รอบ\n",
                (unsigned long)dbg.getUInt("upS", 0), (unsigned long)dbg.getUInt("acked", 0),
                (unsigned long)dbg.getUInt("noAck", 0));
  Serial.printf("สะสม: บูต %lu ครั้ง, BROWNOUT %lu ครั้ง, PANIC %lu ครั้ง, WATCHDOG %lu ครั้ง  (ส่ง c ใน Serial = ล้าง)\n",
                (unsigned long)boots, (unsigned long)brown, (unsigned long)panics, (unsigned long)wdts);
  Serial.println("=======================================");
  dbg.putUInt("boots", boots);
  dbg.putUInt("brown", brown);
  dbg.putUInt("panic", panics);
  dbg.putUInt("wdt", wdts);
  dbg.putUInt("upS", 0);
  dbg.putUInt("acked", 0);
  dbg.putUInt("noAck", 0);
  dbg.putUChar("stage", 0);
  lastResetBrownout = (reason == ESP_RST_BROWNOUT);
}

void debugSave() {
  static uint32_t lastSave = 0;
  if (millis() - lastSave < SAVE_EVERY_MS) return;
  lastSave = millis();
  dbg.putUInt("upS", millis() / 1000);
  dbg.putUInt("acked", ackedCount);
  dbg.putUInt("noAck", noAckRounds);
}

void setup() {
#if DISABLE_BROWNOUT
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // ปิดตัวตรวจไฟตก (ดูคำเตือนด้านบน)
#endif
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  delay(300);
  debugBoot();
  blink(3, 80, 120);
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
  scalePrefs.begin("scale", false);
  if (!scale.wait_ready_timeout(2000)) Serial.println("HX711 not responding (เช็คสาย DT/SCK/VCC) -> weight = NaN");
  else if (scalePrefs.isKey("offset")) {
    scale.set_offset(scalePrefs.getLong("offset"));
    Serial.printf("[SCALE] ใช้จุดศูนย์เดิมจาก flash offset=%ld (พิมพ์ t เพื่อ tare ใหม่)\n", (long)scale.get_offset());
  } else retare("บูตครั้งแรก");
#endif

  uint32_t radioDelay = lastResetBrownout ? RADIO_DELAY_AFTER_BROWNOUT_MS : RADIO_DELAY_MS;
  Serial.printf("CPU %lu MHz, รอ %lu ms ก่อนเปิดวิทยุ%s\n", (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)radioDelay, DISABLE_BROWNOUT ? " (ปิดตัวตรวจไฟตก)" : "");
  delay(radioDelay);
  markStage(1);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (TX_POWER_QDBM > 0) esp_wifi_set_max_tx_power(TX_POWER_QDBM);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    while (true) blink(10, 50, 50);          // กะพริบรัวไม่หยุด = ESP-NOW เริ่มไม่ได้
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, GATEWAY_MAC, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("add peer FAILED");
  bootId = (uint16_t)esp_random();
  markStage(2);
  blink(2, 300, 200);                         // กะพริบช้า 2 ครั้ง = เปิดวิทยุผ่านแล้ว
  // watchdog: ถ้า loop() ค้างเกิน ~5 วิ (เช่นไฟตกจนชิปเพี้ยนแต่ไม่รีเซ็ต) ให้รีเซ็ตตัวเองแล้วเริ่มใหม่
  // บูตครั้งถัดไปจะขึ้นว่า "WATCHDOG (ค้าง)" ใน node debug
  enableLoopWDT();
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
#else
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') { dbg.clear(); Serial.println("ล้างสถิติ debug แล้ว"); }
    if (c == 't') retare("สั่งจาก Serial");
  }
#endif
  debugSave();
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
  uint32_t ackedBefore = ackedCount;
  flushQueue();
  if (seq == 1) markStage(3);
  if (ackedCount > ackedBefore) {
    blink(1, 150, 0);                       // gateway ตอบแล้ว
  } else {
    noAckRounds++;
    digitalWrite(STATUS_LED, HIGH); delay(1000); digitalWrite(STATUS_LED, LOW);  // ส่งไม่ถึง
  }

  Serial.printf("%sseq=%lu  w=%.1f g  d=%.1f cm  | acked=%lu resent=%lu queued=%d dropped=%lu macFail=%lu\n",
                MOCK_WEIGHT ? "[MOCK] " : "", msg.seq, msg.weight_g, msg.dist_cm,
                ackedCount, retryCount, qCount, droppedCount, macFailCount);
}
