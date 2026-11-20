// gateway/gateway.ino — FreeRTOS (E1) + ACK/offline queue (E4) + TLS root CA / Firebase Auth (E5)
//
//  ESP-NOW callback ──rxQueue──▶ nodeTask ──uploadQueue──▶ netTask ──HTTPS──▶ Firebase
//        ▲  ACK ◀──────────────────┘   │ ตรวจการดื่ม                   │ เน็ตล่ม → flash (LittleFS)
//  sensorTask (DHT11, KY-038) ─┐       │                               └── lineQueue → LINE
//  PIR / e-stop ISR ───────────┼─▶ state (mutex) ◀── aiTask ──servoQueue──▶ servoTask ──▶ SG90
//
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include <Preferences.h>
#include "msg.h"
#include "secrets.h"
#include "root_ca.h"        // ต้องมาก่อน line_push.h / cloud.h (E5)
#include "line_push.h"
#include "cloud.h"
#include "drink_detect.h"
#include "ai_model.h"
#include "line_notify.h"   // ตัดสินใจเวลา/ระดับ/ข้อความ LINE ตามค่า AI

// Arduino IDE สร้าง prototype ของฟังก์ชันไว้บนสุดของไฟล์อัตโนมัติ
// ต้องประกาศชื่อ struct ไว้ก่อน ไม่งั้น linkStatFor() จะ error ว่า LinkStat does not name a type
struct LinkStat;

const int DHT_PIN = 14, PIR_PIN = 27, SND_AO = 35, SND_DO = 32;
const int SERVO_PIN = 18, STOP_BTN = 33, LED = 2;

// ===== ตั้งค่า AI / ป้าย / LINE =====
// DEMO_SPEED: 1 = เวลาจริง, 60 = เร่งเวลา 60 เท่า (1 วินาที = 1 นาที) ไว้ถ่ายวิดีโอ
// ตอนเร่งเวลา ข้อมูลที่ส่งขึ้น Firebase จะติด demo:true และ ai/train.py จะไม่เอาไปเทรน
#ifndef DEMO_SPEED
#define DEMO_SPEED 1
#endif
const float    AI_P_UP        = 0.70f;  // ความเสี่ยง ≥ นี้ → ยกป้าย (บนชุด test: ป้ายยก ~27 % ของเวลา, ถูก 72 %)
const float    AI_P_DOWN      = 0.62f;  // ความเสี่ยง ≤ นี้ → ลดป้าย (ห่างกันไว้กันป้ายกระพือ)
const float    HOLD_DOWN_MIN  = 15;     // ดื่มแล้ว ป้ายลงค้างอย่างน้อยกี่นาที
// LINE: ปรับละเอียดได้ใน LineNotifyConfig (line_notify.h) ค่าด้านล่างถูกนำไปใส่ใน setup()
const float    LINE_AFTER_MIN   = 10;   // ป้ายยกค้างกี่นาทีแล้วยังไม่ดื่ม → ส่ง LINE ครั้งแรก (นาที คูณ DEMO_SPEED)
const float    LINE_REPEAT_MIN  = 30;   // ยังไม่ดื่ม → เตือนซ้ำห่างกันกี่นาที
const float    LINE_MIN_GAP_MIN = 20;   // ทุกข้อความห่างกันอย่างน้อย (กันโควตาหมด)
const int      LINE_MAX_PER_DAY = 6;    // เพดานข้อความอัตโนมัติต่อวัน
// ส่งหาใคร: ตั้ง LINE_USER_ID ใน secrets.h (LINE Developers → Messaging API → "Your user ID") = ส่งเฉพาะเจ้าของ (push)
// ถ้าไม่ตั้ง จะ broadcast หาเพื่อนทุกคนของ Official Account เหมือนเดิม
#ifndef LINE_USER_ID
#define LINE_USER_ID ""
#endif
const float    PRESENCE_CM    = 80;     // ultrasonic < นี้ = มีคนนั่งหน้าแก้ว
const uint32_t PIR_WINDOW_MS  = 5UL * 60UL * 1000UL;     // PIR ขยับภายใน 5 นาที = มีคน
const int      SIGN_UP_DEG = 90, SIGN_DOWN_DEG = 0;
// SERVO_MODE: เลือกตามรุ่นเซอร์โวที่ใช้
//   0 = ไม่ส่งพัลส์เลย เซอร์โวหยุดนิ่ง (AI ยังสั่งยก/ลดป้ายใน Serial และ dashboard ตามปกติ)
//   1 = รุ่น 180° สั่งเป็นมุม SIGN_UP_DEG / SIGN_DOWN_DEG
//   2 = รุ่น 360° (ตัวที่ใช้อยู่) พัลส์คุมความเร็ว ไม่ได้คุมมุม เลยหมุนตามเวลา SERVO_MOVE_MS แล้วหยุด
#ifndef SERVO_MODE
#define SERVO_MODE 2
#endif
// ใช้เฉพาะ SERVO_MODE 2
const int      SERVO_STOP_US  = 1500;  // พัลส์ที่เซอร์โวหยุด ใช้คำนวณทิศและความเร็ว
const int      SERVO_SPEED_US = 200;   // ห่างจากจุดหยุดเท่านี้ = ความเร็ว (มาก = เร็ว ไม่เกินราว 900)
const uint32_t SERVO_MOVE_MS  = 480;   // หมุนนานเท่านี้แล้วหยุด ปรับจนป้ายยกได้มุมที่ต้องการ
const int      SERVO_UP_DIR   = -1;    // ทิศตอนยกป้าย (หันหน้าเข้าด้านที่มีธง) 1 = ธงพักด้านขวา -1 = ธงพักด้านซ้าย
const uint32_t AI_EVERY_MS = 10000;     // คำนวณ AI ทุก 10 วินาที
// อุ่นเครื่องหลังบูต: ช่วงนี้ AI คำนวณได้แต่ห้ามยกป้าย
// เพราะยังไม่มีข้อมูลจาก node / PIR ยังไม่นิ่ง → AI จะเข้าใจว่า "ไม่มีคน" (present=0) แล้วยกป้ายทันทีหลังอัปโค้ด
// เวลาจริง ไม่คูณ DEMO_SPEED (PIR ส่วนใหญ่ต้องอุ่นเครื่อง ~30–60 วินาที)
const uint32_t AI_WARMUP_MS = 60000;

// ===== กติกาสำหรับใช้งานจริง (ดู docs/real-world-cases.md) =====
// ทุกค่าเป็นเวลาจริง ยกเว้นที่เขียนว่า "นาที (คูณ DEMO_SPEED)"
const bool     REQUIRE_PRESENT   = true;    // ยกป้ายเฉพาะตอนมีคนอยู่หน้าแก้ว (ไม่มีคนเห็น ยกไปก็เปล่า)
const float    MAX_NO_DRINK_MIN  = 90;      // นาที (คูณ DEMO_SPEED) ไม่ได้ดื่มนานเท่านี้ ยกป้ายเลยไม่ต้องรอ AI
const uint32_t NODE_STALE_MS     = 60000;   // node เงียบเกินนี้ = ออฟไลน์/แบตหมด → ไม่รู้ว่าดื่มหรือยัง ห้ามยกป้ายใหม่
const uint32_t CUP_AWAY_GRACE_MS = 180000;  // ยกแก้วออกไม่เกิน 3 นาที = น่าจะกำลังดื่ม ห้ามยกป้าย
const uint32_t MIN_DWELL_MS      = 60000;   // ป้ายต้องค้างอย่างน้อย 1 นาทีก่อน AI จะเปลี่ยนอีก (กันกระพือ) การดื่มไม่ต้องรอ
// ช่วงเวลาเงียบ (ชั่วโมงไทย): ไม่ยกป้าย ไม่ส่ง LINE และลดป้ายลง  ตั้ง START == END เพื่อปิด
// ตอน DEMO_SPEED != 1 (ถ่ายวิดีโอ) จะไม่ใช้ช่วงเวลาเงียบ
const int      QUIET_START_H     = 22;
const int      QUIET_END_H       = 7;
const uint32_t SENSE_EVERY_MS = 5000;   // อ่าน DHT11 / เสียง ทุก 5 วินาที
const uint32_t RETRY_AGE_MS = 200;
// =====================================

DHT dht(DHT_PIN, DHT11);

// ---------------- FreeRTOS objects (E1) ----------------
struct RxItem { NodeMsg m; int rssi; uint8_t mac[6]; };
QueueHandle_t rxQueue;       // ESP-NOW callback → nodeTask
QueueHandle_t uploadQueue;   // nodeTask → netTask  (UpItem)
QueueHandle_t servoQueue;    // ใครก็ได้ → servoTask (มุมล่าสุด, ยาว 1 ใช้ xQueueOverwrite)
QueueHandle_t lineQueue;     // aiTask / Serial → netTask (ข้อความ LINE)
SemaphoreHandle_t stateMutex;
struct LineMsg { char text[480]; bool autoAlert; uint32_t drinkStamp; };   // ไทย 3 ไบต์/ตัว จึงต้องใหญ่ ; drinkStamp = lastDrinkMs ตอนสร้าง ไว้เช็คว่าดื่มไปแล้วหรือยัง

// ---------------- interrupt (E2) ----------------
volatile uint32_t motionCount = 0, lastMotionMs = 0;
volatile bool estop = false;
void IRAM_ATTR onMotion() { motionCount = motionCount + 1; lastMotionMs = millis(); }
void IRAM_ATTR onStop()   { estop = true; }

// ---------------- state ที่หลาย task ใช้ร่วมกัน (ล็อกด้วย stateMutex) ----------------
struct State {
  float roomT = NAN, roomRH = NAN;
  int   soundP2P = 0;
  float lastDistCm = -1;
  uint32_t lastNodeMs = 0;          // เวลาที่ได้ข้อมูลสด (ไม่ใช่ข้อมูลค้างคิว) จาก node ล่าสุด
  uint32_t cupAwaySinceMs = 0;      // 0 = แก้ววางอยู่
  bool present = false;
  bool quiet = false;
  uint32_t signChangedMs = 0;
  // การดื่ม
  DrinkDetector drink;
  struct { uint32_t ms; float ml; } hist[32];
  int histHead = 0, histCount = 0;
  uint32_t lastDrinkMs = 0;
  float mlToday = 0;
  int todayYday = -1;
  // AI + ป้าย
  float aiX[AI_N_FEATURES] = {0};
  float aiP = NAN;
  bool signUp = false;
  uint32_t signUpSinceMs = 0, holdUntilMs = 0;
  LineNotifier ln;                  // ตัวตัดสินใจส่ง LINE (line_notify.h)
  bool lineSent = false;
  // สถิติ
  uint32_t rxCount = 0, dupCount = 0, uploadDrop = 0;
} S;

struct Lock {
  Lock()  { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(stateMutex); }
};

// นาทีที่ผ่านไป (คูณ DEMO_SPEED แล้ว)
float minsSince(uint32_t ms) { return (millis() - ms) / 60000.0f * DEMO_SPEED; }
float nowMin() { return millis() / 60000.0f * DEMO_SPEED; }       // "นาทีในระบบ" ให้ LineNotifier
uint32_t scaledMinToMs(float m) { return (uint32_t)(m * 60000.0f / DEMO_SPEED); }

// ---------------- servo ----------------
void servoWrite(int deg) {
  if (SERVO_MODE != 1) { ledcWrite(SERVO_PIN, 0); return; }
  uint32_t us = map(constrain(deg, 0, 180), 0, 180, 500, 2400);
  ledcWrite(SERVO_PIN, us * 65535UL / 20000UL);
}

// เรียกตอนถือ Lock อยู่
void setSignLocked(bool up, const char *why) {
  if (estop || up == S.signUp) return;
  S.signUp = up;
  S.lineSent = false;
  if (up) S.signUpSinceMs = millis();
  S.signChangedMs = millis();
  int deg = up ? SIGN_UP_DEG : SIGN_DOWN_DEG;
  xQueueOverwrite(servoQueue, &deg);
  Serial.printf("[SIGN] %s (%s)  p=%.2f\n", up ? "UP" : "DOWN", why, S.aiP);
}

#if SERVO_MODE == 2
// ตอนเปิดเครื่องถือว่าป้ายลงอยู่ ถ้าไม่ใช่ ให้หมุนป้ายลงด้วยมือก่อนเปิด
void servoTask(void *) {
  int target = SIGN_DOWN_DEG;
  int current = SIGN_DOWN_DEG;   // ตำแหน่งที่ป้ายอยู่ตอนนี้
  int movingTo = SIGN_DOWN_DEG;
  bool moving = false;
  uint32_t moveStartMs = 0;
  bool stopped = false;
  ledcWrite(SERVO_PIN, 0);
  for (;;) {
    int deg;
    if (xQueueReceive(servoQueue, &deg, pdMS_TO_TICKS(20)) == pdTRUE) target = deg;
    if (estop) {
      if (!stopped) { ledcWrite(SERVO_PIN, 0); stopped = true; moving = false; Serial.println("[ESTOP] หยุดเซอร์โวแล้ว (พิมพ์ c เพื่อปลด)"); }
      continue;
    }
    if (stopped) stopped = false;
    if (!moving && target != current) {
      int dir = (target == SIGN_UP_DEG) ? SERVO_UP_DIR : -SERVO_UP_DIR;
      uint32_t us = SERVO_STOP_US + dir * SERVO_SPEED_US;
      ledcWrite(SERVO_PIN, us * 65535UL / 20000UL);
      movingTo = target;
      moveStartMs = millis();
      moving = true;
    }
    if (moving && millis() - moveStartMs >= SERVO_MOVE_MS) {
      ledcWrite(SERVO_PIN, 0);
      current = movingTo;
      moving = false;
    }
  }
}
#else
void servoTask(void *) {
  int target = SIGN_DOWN_DEG;
  bool stopped = false;
  servoWrite(target);
  for (;;) {
    int deg;
    if (xQueueReceive(servoQueue, &deg, pdMS_TO_TICKS(50)) == pdTRUE) target = deg;
    if (estop) {
      if (!stopped) { ledcWrite(SERVO_PIN, 0); stopped = true; Serial.println("[ESTOP] หยุดเซอร์โวแล้ว (พิมพ์ c เพื่อปลด)"); }
      continue;
    }
    if (stopped) stopped = false;
    servoWrite(target);
  }
}
#endif

// ---------------- ESP-NOW ----------------
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(NodeMsg)) return;
  RxItem it;
  memcpy(&it.m, data, sizeof(NodeMsg));
  if (it.m.version != MSG_VERSION || it.m.type != MSG_DATA) return;
  it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  memcpy(it.mac, info->src_addr, 6);
  xQueueSend(rxQueue, &it, 0);           // callback ห้ามรอ เต็มก็ทิ้ง (node จะส่งซ้ำเพราะไม่ได้ ACK)
}

void sendAck(const RxItem &it) {
  if (!esp_now_is_peer_exist(it.mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, it.mac, 6);
    peer.channel = 0;                    // 0 = ช่องเดียวกับ Wi-Fi ปัจจุบัน
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  AckMsg a = {MSG_VERSION, MSG_ACK, it.m.node_id, it.m.boot_id, it.m.seq};
  esp_now_send(it.mac, (uint8_t *)&a, sizeof(a));
}

// กันข้อมูลซ้ำ (node ส่งซ้ำเมื่อ ACK หาย) จำ (boot_id, seq) ล่าสุด 32 ค่าต่อ node
bool isDuplicate(const NodeMsg &m, bool *firstDup) {
  static struct { uint8_t node; uint16_t boot; uint32_t seq; bool retried; } seen[32];
  static int head = 0;
  for (auto &s : seen)
    if (s.seq == m.seq && s.boot == m.boot_id && s.node == m.node_id) {
      *firstDup = !s.retried;
      s.retried = true;
      return true;
    }
  seen[head] = {m.node_id, m.boot_id, m.seq, m.age_ms >= RETRY_AGE_MS};
  head = (head + 1) % 32;
  return false;
}

struct LinkStat { bool used; uint8_t node; uint16_t boot; uint32_t retried; };
LinkStat linkStat[8];
LinkStat &linkStatFor(const NodeMsg &m);  // ประกาศเอง IDE จะได้ไม่สร้าง prototype ซ้ำ

LinkStat &linkStatFor(const NodeMsg &m) {
  static int next = 0;
  for (auto &l : linkStat)
    if (l.used && l.node == m.node_id) {
      if (l.boot != m.boot_id) { l.boot = m.boot_id; l.retried = 0; }
      return l;
    }
  LinkStat &l = linkStat[next];
  next = (next + 1) % 8;
  l = {true, m.node_id, m.boot_id, 0};
  return l;
}

// ---------------- การดื่ม ----------------
float mlLastHourLocked() {
  float sum = 0;
  for (int i = 0; i < S.histCount; i++) {
    int idx = (S.histHead - 1 - i + 32) % 32;
    if (minsSince(S.hist[idx].ms) < 60) sum += S.hist[idx].ml;
  }
  return sum;
}

// ใส่เวลา ts (ms) และ key ให้ UpItem  sampleAgeMs = ค่านี้วัดมาแล้วนานเท่าไร
void stampItem(UpItem &it, JsonDocument &doc, uint32_t sampleAgeMs, const char *suffix) {
  if (timeValid()) {
    char ts[24];
    snprintf(ts, sizeof(ts), "%llu", (unsigned long long)(epochMs() - sampleAgeMs));
    doc["ts"] = serialized(String(ts));
    snprintf(it.key, sizeof(it.key), "%s-%s", ts, suffix);
  } else {
    doc["ts"][".sv"] = "timestamp";      // ยังไม่ได้เวลา NTP ให้ Firebase ใส่เวลาเอง
    it.key[0] = 0;
  }
  if (DEMO_SPEED != 1) doc["demo"] = true;
}

void queueUpload(UpItem &it, JsonDocument &doc) {
  if (measureJson(doc) >= sizeof(it.body)) { Serial.println("[UP] JSON ยาวเกิน ทิ้ง"); return; }
  serializeJson(doc, it.body, sizeof(it.body));
  if (xQueueSend(uploadQueue, &it, pdMS_TO_TICKS(200)) != pdTRUE) { Lock l; S.uploadDrop++; }
}

// ---------------- จำยอดดื่มข้ามรีบูต (NVS) ----------------
// gateway ไฟดับ/อัปโค้ดใหม่ ยอดวันนี้กับเวลาดื่มล่าสุดไม่หาย (คืนค่าตอนได้เวลา NTP แล้ว)
Preferences prefs;
void saveDrinkStateLocked() {
  struct tm now;
  if (!timeValid() || !getLocalTime(&now, 0)) return;
  prefs.putInt("yday", now.tm_yday);
  prefs.putFloat("ml", S.mlToday);
  prefs.putULong64("lastDrink", (uint64_t)(epochMs() - (millis() - S.lastDrinkMs)));
}

void restoreDrinkStateLocked(const struct tm &now) {
  uint64_t last = prefs.getULong64("lastDrink", 0);
  if (last && epochMs() > last && epochMs() - last < 7ULL * 86400000ULL) {
    // ลบแบบ unsigned: millis() - อายุ จะวนค่า แต่ minsSince() ลบกลับได้อายุจริง
    S.lastDrinkMs = millis() - (uint32_t)(epochMs() - last);
    Serial.printf("[DRINK] คืนค่าจาก flash: ดื่มล่าสุด %.0f นาทีที่แล้ว\n", (epochMs() - last) / 60000.0);
  }
  if (prefs.getInt("yday", -1) == now.tm_yday) {
    S.mlToday = prefs.getFloat("ml", 0);
    Serial.printf("[DRINK] คืนค่าจาก flash: วันนี้ดื่มไป %.0f ml\n", S.mlToday);
  }
}

void handleDrinkLocked(const NodeMsg &m, DrinkEvent ev, float amount) {
  const bool signWasUp = S.signUp;
  if (ev == EV_DRINK) {
    S.hist[S.histHead].ms = millis();
    S.hist[S.histHead].ml = amount;
    S.histHead = (S.histHead + 1) % 32;
    if (S.histCount < 32) S.histCount++;
    S.lastDrinkMs = millis();
    S.mlToday += amount;
    S.holdUntilMs = millis() + scaledMinToMs(HOLD_DOWN_MIN);
    setSignLocked(false, "ดื่มแล้ว");
    saveDrinkStateLocked();
  }
  if (ev == EV_CUP_BACK && S.signUp) {
    // ยกแก้วขึ้นแล้ววางกลับขณะธงยกอยู่ (น้ำลดไม่ถึงเกณฑ์) ถือว่าดื่มแล้ว: ลดธง นับเวลาดื่มล่าสุดใหม่ และพักธง (ไม่มียอด ml)
    S.lastDrinkMs = millis();
    S.holdUntilMs = millis() + scaledMinToMs(HOLD_DOWN_MIN);
    setSignLocked(false, "ยกแก้ววางกลับ");
    saveDrinkStateLocked();
  }
  if (ev == EV_CUP_LIFTED) S.cupAwaySinceMs = millis() | 1;
  if (ev == EV_CUP_PLACED) S.cupAwaySinceMs = 0;
  switch (ev) {
    case EV_DRINK:       Serial.printf("[DRINK] ดื่ม %.0f ml  (วันนี้ %.0f ml)\n", amount, S.mlToday); break;
    case EV_REFILL:      Serial.printf("[DRINK] เติมน้ำ +%.0f g\n", amount); break;
    case EV_CUP_PLACED:  Serial.printf("[DRINK] วางแก้ว w=%.0f g (รอนิ่ง ~10 วิ แล้วค่อยเทียบ)\n", m.weight_g); break;
    case EV_CUP_LIFTED:  Serial.println("[DRINK] ยกแก้วขึ้น (ยังไม่นับ รอวางกลับ)"); break;
    case EV_CUP_BACK:    Serial.printf("[DRINK] ยกแก้ววางกลับ น้ำลดไม่ถึงเกณฑ์ (%s)\n", signWasUp ? "ลดธงแล้ว" : "ธงไม่ได้ยกอยู่ ไม่ทำอะไร"); break;
    case EV_CUP_CHANGED: Serial.printf("[DRINK] เปลี่ยนแก้ว จำน้ำหนักใหม่ %.0f g\n", S.drink.refWeight()); break;
    default: break;
  }
}

void nodeTask(void *) {
  RxItem it;
  for (;;) {
    if (xQueueReceive(rxQueue, &it, portMAX_DELAY) != pdTRUE) continue;
    sendAck(it);                          // E4: ตอบรับก่อน แม้จะเป็นค่าซ้ำ
    const NodeMsg &m = it.m;
    bool firstDup = false;
    if (isDuplicate(m, &firstDup)) {
      if (firstDup) linkStatFor(m).retried++;
      Lock l; S.dupCount++; continue;
    }
    LinkStat &linkSt = linkStatFor(m);
    if (m.age_ms >= RETRY_AGE_MS) linkSt.retried++;

    UpItem up = {};
    JsonDocument rec;
    DrinkEvent ev;
    float amount = 0;
    float refBefore = NAN;                 // น้ำหนักนิ่งก่อนหน้า (ก่อนดื่ม/เติม) ไว้โชว์บน dashboard
    {
      Lock l;
      S.rxCount++;
      if (m.queued == 0 && m.age_ms < 2000) {        // ข้อมูลค้างคิวเป็นค่าเก่า ไม่ใช้บอกว่าตอนนี้มีคนไหม
        S.lastDistCm = m.dist_cm;
        S.lastNodeMs = millis();
      }
      refBefore = S.drink.refWeight();
      ev = S.drink.update(m.weight_g, &amount);
      if (ev != EV_NONE) handleDrinkLocked(m, ev, amount);
      if (isnan(refBefore) && !isnan(S.drink.refWeight()))
        Serial.printf("[DRINK] จำน้ำหนักแก้วแรก %.0f g (ดื่มแล้ววางกลับ ลดลง ≥ 15 g จะนับ)\n", S.drink.refWeight());

      rec["node_id"]   = m.node_id;
      rec["seq"]       = m.seq;
      rec["weight_g"]  = m.weight_g;
      rec["dist_cm"]   = m.dist_cm;
      rec["batt_mv"]   = m.batt_mv;
      rec["rssi"]      = it.rssi;
      if (m.age_ms) rec["age_ms"] = m.age_ms;          // มาช้าเพราะค้างในคิว node
      rec["pkts_retried"] = linkSt.retried;
      rec["room_t"]    = S.roomT;
      rec["room_rh"]   = S.roomRH;
      rec["sound_p2p"] = S.soundP2P;
      rec["motion_count"] = (uint32_t)motionCount;
      rec["cup"]       = S.drink.cupPresent();
      rec["ml_today"]  = S.mlToday;
      rec["mins_since_drink"] = minsSince(S.lastDrinkMs);
      if (!isnan(S.aiP)) {
        rec["ai_p"] = S.aiP;
        JsonArray x = rec["ai_x"].to<JsonArray>();     // feature ที่ AI ใช้ เก็บไว้เทรนใหม่ (ai/train.py)
        for (int i = 0; i < AI_N_FEATURES; i++) x.add(S.aiX[i]);
      }
      rec["sign_up"]   = S.signUp;
      rec["present"]   = S.present;
      rec["estop"]     = (bool)estop;
      rec["line_sent"] = S.lineSent;
    }
    Serial.printf("RX node=%d seq=%lu w=%.1f d=%.1f rssi=%d age=%lums queued=%u\n",
                  m.node_id, (unsigned long)m.seq, m.weight_g, m.dist_cm, it.rssi,
                  (unsigned long)m.age_ms, m.queued);

    char suffix[16];
    snprintf(suffix, sizeof(suffix), "%u-%lu", m.node_id, (unsigned long)m.seq);
    strlcpy(up.path, "readings", sizeof(up.path));
    up.latest = (m.queued == 0);          // ค่าล่าสุดจริง ๆ เท่านั้นที่ทับ /latest
    stampItem(up, rec, m.age_ms, suffix);
    queueUpload(up, rec);

    if (ev == EV_DRINK || ev == EV_REFILL) {
      UpItem d = {};
      JsonDocument doc;
      doc["node_id"] = m.node_id;
      doc["type"]    = drinkEventName(ev);
      doc["ml"]      = amount;                       // = น้ำหนักที่หายไป (g) ≈ ml
      if (!isnan(refBefore)) {
        doc["w_from"] = roundf(refBefore * 10) / 10;                                   // น้ำหนักก่อนดื่ม
        doc["w_to"]   = roundf((ev == EV_DRINK ? refBefore - amount : refBefore + amount) * 10) / 10;  // หลังดื่ม
      }
      { Lock l; doc["ml_today"] = S.mlToday; }
      strlcpy(d.path, ev == EV_DRINK ? "drinks" : "refills", sizeof(d.path));
      stampItem(d, doc, m.age_ms, suffix);
      queueUpload(d, doc);
    }
  }
}

// ---------------- เซนเซอร์ gateway ----------------
int readSound() {
  int lo = 4095, hi = 0; uint32_t t0 = millis();
  while (millis() - t0 < 50) { int v = analogRead(SND_AO); lo = min(lo, v); hi = max(hi, v); vTaskDelay(1); }
  return hi - lo;
}

void sensorTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    float t = dht.readTemperature(), h = dht.readHumidity();
    int snd = readSound();
    struct tm now;
    bool haveTime = getLocalTime(&now, 0);
    {
      Lock l;
      if (!isnan(t)) S.roomT = t;
      if (!isnan(h)) S.roomRH = h;
      S.soundP2P = snd;
      if (haveTime && S.todayYday == -1) {               // เพิ่งได้เวลาหลังบูต → คืนค่าที่จำไว้
        S.todayYday = now.tm_yday;
        restoreDrinkStateLocked(now);
      } else if (haveTime && S.todayYday != now.tm_yday) {  // วันใหม่ → รีเซ็ตยอดวันนี้
        Serial.println("[DRINK] วันใหม่ รีเซ็ตยอดวันนี้");
        S.todayYday = now.tm_yday;
        S.mlToday = 0;
        saveDrinkStateLocked();
      }
      S.quiet = false;
      if (haveTime && DEMO_SPEED == 1 && QUIET_START_H != QUIET_END_H) {
        int h = now.tm_hour;
        S.quiet = QUIET_START_H < QUIET_END_H ? (h >= QUIET_START_H && h < QUIET_END_H)
                                              : (h >= QUIET_START_H || h < QUIET_END_H);
      }
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSE_EVERY_MS));
  }
}

// ---------------- AI ----------------
// ลำดับต้องตรงกับ FEATURES ใน ai/train.py
void computeFeaturesLocked(float *x) {
  bool pir  = lastMotionMs != 0 && millis() - lastMotionMs < PIR_WINDOW_MS;
  // < 5 cm = ค่าผิด (HC-SR04 วัดใกล้กว่า ~2–4 cm ไม่ได้ มักเป็นเสียงสะท้อนผนังกล่อง) ไม่นับว่ามีคน
  bool nodeFresh = S.lastNodeMs != 0 && millis() - S.lastNodeMs < NODE_STALE_MS;
  bool near = nodeFresh && S.lastDistCm >= 5 && S.lastDistCm < PRESENCE_CM;   // ค่าเก่าจาก node ที่ดับไปแล้ว ไม่นับ
  S.present = pir || near;
  x[0] = min(minsSince(S.lastDrinkMs), 240.0f);
  x[1] = mlLastHourLocked();
  x[2] = S.mlToday;
  x[3] = isnan(S.roomT)  ? AI_MEAN[3] : S.roomT;    // DHT ยังไม่มีค่า ใช้ค่าเฉลี่ยแทน
  x[4] = isnan(S.roomRH) ? AI_MEAN[4] : S.roomRH;
  x[5] = (pir || near) ? 1 : 0;
  x[6] = S.soundP2P;
  // ค่าเพี้ยน (DHT11 อ่านผิด, เสียงดังกระแทก) ห้ามดัน AI เกินช่วงที่เคยเห็นตอนเทรน: ตัดไว้ที่ เฉลี่ย ± 3 SD
  for (int i = 1; i < AI_N_FEATURES; i++)
    x[i] = constrain(x[i], AI_MEAN[i] - 3 * AI_STD[i], AI_MEAN[i] + 3 * AI_STD[i]);
  x[1] = max(x[1], 0.0f); x[2] = max(x[2], 0.0f); x[6] = max(x[6], 0.0f);
}

void printAILocked() {
  const char *blk = raiseBlockedLocked();
  Serial.printf("[AI] p=%.2f sign=%s%s%s |", S.aiP, S.signUp ? "UP" : "DOWN",
                blk ? " (ห้ามยก: " : "", blk ? blk : "");
  if (blk) Serial.print(")");
  for (int i = 0; i < AI_N_FEATURES; i++) Serial.printf(" %s=%.1f", AI_FEATURE_NAMES[i], S.aiX[i]);
  Serial.println();
}

// ยังห้ามยกป้ายอยู่ไหม คืนเหตุผล (nullptr = ยกได้)
const char *raiseBlockedLocked() {
  if (estop)                   return "e-stop";
  if (millis() < AI_WARMUP_MS) return "อุ่นเครื่องหลังบูต";
  if (S.rxCount == 0)          return "ยังไม่ได้ข้อมูลจาก node";
  if (millis() - S.lastNodeMs >= NODE_STALE_MS) return "node เงียบ (ดับ/แบตหมด/อยู่ไกล)";
  if (S.quiet)                 return "ช่วงเวลาเงียบ (กลางคืน)";
  if ((int32_t)(S.holdUntilMs - millis()) > 0) return "เพิ่งดื่ม (พักป้าย)";
  if (S.cupAwaySinceMs && millis() - S.cupAwaySinceMs < CUP_AWAY_GRACE_MS) return "ยกแก้วอยู่ (น่าจะกำลังดื่ม)";
  if (REQUIRE_PRESENT && !S.present) return "ไม่มีคนหน้าโต๊ะ";
  if (S.signChangedMs && millis() - S.signChangedMs < MIN_DWELL_MS) return "ป้ายเพิ่งเปลี่ยน (กันกระพือ)";
  return nullptr;
}

// อธิบายว่าทำไม AI ได้ค่านี้ (พิมพ์ w ใน Serial หรือพิมพ์เองตอน AI ยก/ลดป้าย)
// แต่ละ feature: ค่าที่วัดได้, ค่าเฉลี่ยตอนเทรน, และ "แรงดัน" (+ = ดันให้ยกป้าย, - = ดันให้ลด)
void printExplainLocked() {
  float z = AI_B;
  Serial.println("----- [WHY] AI อธิบายตัวเอง -----");
  Serial.println("  feature              now     mean   push   (push + = ดันให้ยกป้าย, - = ดันให้ลด)");
  for (int i = 0; i < AI_N_FEATURES; i++) {
    float c = AI_W[i] * (S.aiX[i] - AI_MEAN[i]) / AI_STD[i];
    z += c;
    Serial.printf("  %-17s %7.1f %7.1f %+6.2f %s\n", AI_FEATURE_NAMES[i], S.aiX[i], AI_MEAN[i], c,
                  c > 0.1f ? "<- ดันขึ้น" : (c < -0.1f ? "<- ดันลง" : ""));
  }
  Serial.printf("  ค่าตั้งต้น %+.2f  รวม z=%.2f  -> p=%.2f  (ยกเมื่อ p>=%.2f, ลดเมื่อ p<=%.2f)\n",
                AI_B, z, S.aiP, AI_P_UP, AI_P_DOWN);
  bool pir = lastMotionMs != 0 && millis() - lastMotionMs < PIR_WINDOW_MS;
  long motionAgoS = lastMotionMs ? (long)((millis() - lastMotionMs) / 1000) : -1;   // -1 = ยังไม่เคยขยับ
  Serial.printf("  present: PIR=%s (ขยับล่าสุด %ld วิที่แล้ว, -1=ยังไม่เคย) ultrasonic=%.0f cm (นับว่ามีคนถ้า 5-%.0f)\n",
                pir ? "มีคน" : "ไม่มี", motionAgoS, S.lastDistCm, PRESENCE_CM);
  Serial.printf("  ไม่ได้ดื่ม %.0f นาที (ยกเลยถ้า >= %.0f)  node ล่าสุด %lds ก่อน  แก้ว=%s  quiet=%s\n",
                minsSince(S.lastDrinkMs), MAX_NO_DRINK_MIN,
                S.lastNodeMs ? (long)((millis() - S.lastNodeMs) / 1000) : -1L,
                S.drink.cupPresent() ? "วางอยู่" : "ไม่อยู่บนแท่น", S.quiet ? "ใช่" : "ไม่");
  const char *blk = raiseBlockedLocked();
  Serial.printf("  ป้าย=%s  node rx=%lu  uptime=%lus  %s%s\n", S.signUp ? "UP" : "DOWN",
                (unsigned long)S.rxCount, (unsigned long)(millis() / 1000),
                blk ? "ห้ามยกตอนนี้: " : "AI ยกป้ายได้", blk ? blk : "");
  Serial.println("---------------------------------");
}

void runAIOnce(bool print) {
  LineMsg lm = {};
  bool sendLine = false;
  {
    Lock l;
    computeFeaturesLocked(S.aiX);
    S.aiP = aiPredict(S.aiX);
    bool wasUp = S.signUp;
    bool overdue = minsSince(S.lastDrinkMs) >= MAX_NO_DRINK_MIN;     // กฎตายตัว ไม่ต้องรอ AI
    bool dwellOk = millis() - S.signChangedMs >= MIN_DWELL_MS;
    if (!S.signUp && !raiseBlockedLocked()) {
      if (overdue)                setSignLocked(true, "ไม่ได้ดื่มนานเกิน MAX_NO_DRINK_MIN");
      else if (S.aiP >= AI_P_UP)  setSignLocked(true, "AI: เสี่ยงดื่มน้อย");
    } else if (S.signUp) {
      if (S.quiet)                                  setSignLocked(false, "ช่วงเวลาเงียบ");
      else if (dwellOk && !overdue && S.aiP <= AI_P_DOWN) setSignLocked(false, "AI: ความเสี่ยงลดลง");
    }
    if (S.signUp != wasUp) printExplainLocked();     // AI เปลี่ยนป้าย → บอกเหตุผลทุกครั้ง

    // ป้ายยกค้างนานแล้วยังไม่ดื่ม → LINE  ระดับ (สุภาพ/ด่วน) และข้อความขึ้นกับ p ของ AI (ดู line_notify.h)
    LineNotifyInput ni;
    ni.signUp = S.signUp;  ni.quiet = S.quiet;  ni.p = S.aiP;
    ni.minsSinceDrink = minsSince(S.lastDrinkMs);
    ni.minsSignUp = S.signUp ? minsSince(S.signUpSinceMs) : 0;
    ni.mlLastHour = S.aiX[1];  ni.mlToday = S.mlToday;  ni.roomT = S.roomT;
    ni.dayId = S.todayYday;
    // แจ้ง LINE ทันทีตอนที่ AI/กฎตายตัวเพิ่งยกธง (ข้อความสั้น 1-2 บรรทัด บอกเหตุผลของ AI)
    // ยกด้วยมือ (Serial 'u') ไม่แจ้ง เพราะ wasUp จะเป็น true ในรอบถัดไป ไม่ใช่การยกในรอบนี้
    LineLevel lv = LN_NONE;
    if (S.signUp && !wasUp && S.ln.decideRaise(ni, nowMin())) {
      float c[AI_N_FEATURES];
      for (int i = 0; i < AI_N_FEATURES; i++) c[i] = AI_W[i] * (S.aiX[i] - AI_MEAN[i]) / AI_STD[i];
      LineRaiseInput ri;
      ri.p = S.aiP;  ri.minsSinceDrink = ni.minsSinceDrink;
      ri.overdue = overdue;  ri.overdueMin = MAX_NO_DRINK_MIN;
      ri.x = S.aiX;  ri.c = c;
      lineBuildRaiseText(lm.text, sizeof(lm.text), ri);
      lm.autoAlert = true;
      lm.drinkStamp = S.lastDrinkMs;
      S.ln.markRaiseQueued(nowMin());
      sendLine = true;
      Serial.printf("[LINE] สร้างข้อความ RAISE (ยกธง)  p=%.2f\n", S.aiP);
    } else {
      lv = S.ln.decide(ni, nowMin());
    }
    if (lv != LN_NONE) {
      lineBuildText(lm.text, sizeof(lm.text), lv, ni, S.ln.sentInEpisode());
      lm.autoAlert = true;
      lm.drinkStamp = S.lastDrinkMs;
      S.ln.markQueued(nowMin());
      sendLine = true;
      Serial.printf("[LINE] สร้างข้อความ %s  p=%.2f\n", lv == LN_URGENT ? "URGENT" : "GENTLE", S.aiP);
    }
    if (print) printAILocked();
  }
  if (sendLine && xQueueSend(lineQueue, &lm, 0) != pdTRUE) {   // คิวเต็ม → ยกเลิก ไม่ให้ค้างสถานะ pending
    Lock l; S.ln.onCancel();
  }
}

void aiTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    runAIOnce(true);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(AI_EVERY_MS));
  }
}

// ---------------- เน็ต: Wi-Fi + Firebase + flash queue + LINE ----------------
void netTask(void *) {
  cloudBegin();
  storeBegin();
  uint32_t lastWifiTry = 0;
  for (;;) {
    bool online = WiFi.status() == WL_CONNECTED;
    if (!online && millis() - lastWifiTry > 10000) {      // Robustness: ต่อใหม่ทุก 10 วิ
      lastWifiTry = millis();
      Serial.println("[NET] WiFi lost, reconnecting");
      WiFi.reconnect();
    }

    UpItem it;
    if (xQueueReceive(uploadQueue, &it, pdMS_TO_TICKS(500)) == pdTRUE) {
      digitalWrite(LED, HIGH);
      bool ok = false;
      if (online && storeCount == 0) ok = fbUpload(it);
      if (!ok) {
        // เน็ตล่ม / ส่งไม่สำเร็จ / มีของค้างใน flash อยู่แล้ว (ต่อท้ายไว้ รักษาลำดับเวลา)
        if (online && it.latest) fbRequest("PUT", "/latest", it.body);   // ให้ dashboard เห็นค่าล่าสุดไว้ก่อน
        storeAppend(it);
      }
      digitalWrite(LED, LOW);
    } else if (online && storeCount > 0) {
      storeFlush(10);                                       // ว่าง → ไล่ส่งของค้าง
    }

    LineMsg lm;
    if (xQueueReceive(lineQueue, &lm, 0) == pdTRUE) {
      bool stale = false;
      if (lm.autoAlert) {                // ระหว่างรอส่ง ผู้ใช้ดื่มไปแล้ว / ป้ายลงแล้ว → ไม่ส่งข้อความเตือนที่ล้าสมัย
        Lock l;
        stale = !S.signUp || S.quiet || S.lastDrinkMs != lm.drinkStamp;
        if (stale) S.ln.onCancel();
      }
      if (stale) {
        Serial.println("[LINE] ยกเลิก: ดื่มแล้ว/ป้ายลงระหว่างรอส่ง");
      } else {
        String resp;
        const bool toUser = strlen(LINE_USER_ID) > 0;
        int code = toUser ? linePush(LINE_TOKEN, LINE_USER_ID, lm.text, &resp)
                          : lineBroadcast(LINE_TOKEN, lm.text, &resp);
        Serial.printf("[LINE] %s -> %d\n", toUser ? "push" : "broadcast", code);
        if (code != 200 && resp.length()) Serial.println(resp);
        if (lm.autoAlert) {
          Lock l;
          S.ln.onResult(code == 200, nowMin());
          if (code == 200 && S.signUp) S.lineSent = true;
        }
      }
    }
  }
}

// ---------------- Serial (loop) ----------------
void printStatus() {
  Lock l;
  Serial.printf("[STATUS] rx=%lu dup=%lu upDrop=%lu fbOk=%lu fbFail=%lu flash=%lu(drop %lu) wifi=%s heap=%lu\n",
                (unsigned long)S.rxCount, (unsigned long)S.dupCount, (unsigned long)S.uploadDrop,
                (unsigned long)fbOk, (unsigned long)fbFail, (unsigned long)storeCount,
                (unsigned long)storeDropped, WiFi.status() == WL_CONNECTED ? "ok" : "DOWN",
                (unsigned long)ESP.getFreeHeap());
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'm') {
      LineMsg lm = {};
      strlcpy(lm.text, "ถึงเวลาดื่มน้ำแล้ว 💧 (ทดสอบ)", sizeof(lm.text));
      xQueueSend(lineQueue, &lm, 0);
    } else if (c == 'q') {
      int usage = lineConsumption(LINE_TOKEN);
      if (usage >= 0) Serial.printf("LINE messages used this month: %d\n", usage);
      else Serial.println("Failed to read LINE message usage");
    } else if (c == 'l') {
      Lock l;
      Serial.printf("[LINE] วันนี้ส่งแล้ว %d/%d  รอบนี้ %d ข้อความ  pending=%s  ปลายทาง=%s\n",
                    S.ln.sentToday(), S.ln.cfg.maxPerDay, S.ln.sentInEpisode(),
                    S.ln.pending() ? "ใช่" : "ไม่", strlen(LINE_USER_ID) ? "push เจ้าของ" : "broadcast");
    } else if (c == 'a') { runAIOnce(true); }
    else if (c == 'w') { runAIOnce(false); Lock l; printExplainLocked(); }
    else if (c == 'u') { Lock l; setSignLocked(true, "สั่งจาก Serial"); }
    else if (c == 'n') { Lock l; setSignLocked(false, "สั่งจาก Serial"); }
    else if (c == 'c') {
      estop = false;
      int deg; { Lock l; deg = S.signUp ? SIGN_UP_DEG : SIGN_DOWN_DEG; }
      xQueueOverwrite(servoQueue, &deg);
      Serial.println("[ESTOP] ปลดหยุดฉุกเฉินแล้ว");
    }
    else if (c == 's') { printStatus(); }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(SND_DO, INPUT);
  pinMode(STOP_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotion, RISING);
  attachInterrupt(digitalPinToInterrupt(STOP_BTN), onStop, FALLING);
  ledcAttach(SERVO_PIN, 50, 16);
  dht.begin();
  prefs.begin("coaster", false);

  rxQueue     = xQueueCreate(10, sizeof(RxItem));
  uploadQueue = xQueueCreate(6, sizeof(UpItem));
  servoQueue  = xQueueCreate(1, sizeof(int));
  lineQueue   = xQueueCreate(2, sizeof(LineMsg));
  S.ln.cfg.firstAfterMin  = LINE_AFTER_MIN;
  S.ln.cfg.repeatAfterMin = LINE_REPEAT_MIN;
  S.ln.cfg.minGapMin      = LINE_MIN_GAP_MIN;
  S.ln.cfg.urgentNoDrinkMin = MAX_NO_DRINK_MIN;   // ตรงกับกฎตายตัวที่ยกป้ายเลย
  S.ln.cfg.maxPerDay      = LINE_MAX_PER_DAY;
  stateMutex  = xSemaphoreCreateMutex();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // ปิดโหมดประหยัดไฟ Wi-Fi ไม่งั้นวิทยุหลับเป็นช่วง ๆ แล้วพลาดแพ็กเก็ต ESP-NOW จาก node (macFail)
  WiFi.setAutoReconnect(true);
  Serial.printf("Gateway MAC = %s  <- ต้องตรงกับ GATEWAY_MAC ใน node.ino\n", WiFi.macAddress().c_str());
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  // รอ Wi-Fi สูงสุด 20 วิ ไม่ได้ก็ทำงานต่อ (ข้อมูลเก็บลง flash ไว้ก่อน)
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWiFi OK  IP=%s  WiFi channel = %ld  <- ใส่เลขนี้ใน node\n",
                  WiFi.localIP().toString().c_str(), (long)WiFi.channel());
  else
    Serial.println("\nWiFi ยังไม่ติด ทำงานต่อแบบ offline (node ต้องตั้ง channel ตรงกับ router)");
  configTzTime("ICT-7", "pool.ntp.org", "time.google.com");   // เวลาไทย ใช้รีเซ็ตยอดดื่ม + timestamp

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); while (true) delay(1000); }
  esp_now_register_recv_cb(onRecv);

  //                       ฟังก์ชัน     ชื่อ        stack   param prio handle
  xTaskCreate(servoTask,  "servo",  2048,  nullptr, 4, nullptr);
  xTaskCreate(nodeTask,   "node",   6144,  nullptr, 3, nullptr);
  xTaskCreate(sensorTask, "sensor", 3072,  nullptr, 2, nullptr);
  xTaskCreate(aiTask,     "ai",     6144,  nullptr, 2, nullptr);
  xTaskCreate(netTask,    "net",    12288, nullptr, 1, nullptr);   // HTTPS/TLS ใช้ stack เยอะ

  Serial.printf("Gateway ready  (AI model: %s, DEMO_SPEED=%d)\n", AI_TRAINED_ON, DEMO_SPEED);
  Serial.println("Serial: a=ดูค่า AI  w=AI อธิบายว่าทำไมยก/ไม่ยก  s=สถานะ  u/n=ยก/ลดป้าย  c=ปลด e-stop  m=ส่ง LINE ทดสอบ  l=สถานะ LINE  q=โควตา LINE");
}

void loop() {
  handleSerialCommands();
  vTaskDelay(pdMS_TO_TICKS(20));
}