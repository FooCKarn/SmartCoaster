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
#include "msg.h"
#include "secrets.h"
#include "root_ca.h"        // ต้องมาก่อน line_push.h / cloud.h (E5)
#include "line_push.h"
#include "cloud.h"
#include "drink_detect.h"
#include "ai_model.h"

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
const float    LINE_AFTER_MIN = 10;     // ป้ายยกค้างกี่นาทีแล้วยังไม่ดื่ม → ส่ง LINE
const uint32_t LINE_COOLDOWN_MS = 30UL * 60UL * 1000UL;  // ส่ง LINE ห่างกันอย่างน้อย 30 นาที (เวลาจริง กันโควตาหมด)
const float    PRESENCE_CM    = 80;     // ultrasonic < นี้ = มีคนนั่งหน้าแก้ว
const uint32_t PIR_WINDOW_MS  = 5UL * 60UL * 1000UL;     // PIR ขยับภายใน 5 นาที = มีคน
const int      SIGN_UP_DEG = 90, SIGN_DOWN_DEG = 0;
const uint32_t AI_EVERY_MS = 10000;     // คำนวณ AI ทุก 10 วินาที
const uint32_t SENSE_EVERY_MS = 5000;   // อ่าน DHT11 / เสียง ทุก 5 วินาที
// =====================================

DHT dht(DHT_PIN, DHT11);

// ---------------- FreeRTOS objects (E1) ----------------
struct RxItem { NodeMsg m; int rssi; uint8_t mac[6]; };
QueueHandle_t rxQueue;       // ESP-NOW callback → nodeTask
QueueHandle_t uploadQueue;   // nodeTask → netTask  (UpItem)
QueueHandle_t servoQueue;    // ใครก็ได้ → servoTask (มุมล่าสุด, ยาว 1 ใช้ xQueueOverwrite)
QueueHandle_t lineQueue;     // aiTask / Serial → netTask (ข้อความ LINE)
SemaphoreHandle_t stateMutex;
struct LineMsg { char text[200]; };

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
  uint32_t lastLineMs = 0;
  // สถิติ
  uint32_t rxCount = 0, dupCount = 0, uploadDrop = 0;
} S;

struct Lock {
  Lock()  { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(stateMutex); }
};

// นาทีที่ผ่านไป (คูณ DEMO_SPEED แล้ว)
float minsSince(uint32_t ms) { return (millis() - ms) / 60000.0f * DEMO_SPEED; }
uint32_t scaledMinToMs(float m) { return (uint32_t)(m * 60000.0f / DEMO_SPEED); }

// ---------------- servo ----------------
void servoWrite(int deg) {
  uint32_t us = map(constrain(deg, 0, 180), 0, 180, 500, 2400);
  ledcWrite(SERVO_PIN, us * 65535UL / 20000UL);
}

// เรียกตอนถือ Lock อยู่
void setSignLocked(bool up, const char *why) {
  if (estop || up == S.signUp) return;
  S.signUp = up;
  if (up) S.signUpSinceMs = millis();
  int deg = up ? SIGN_UP_DEG : SIGN_DOWN_DEG;
  xQueueOverwrite(servoQueue, &deg);
  Serial.printf("[SIGN] %s (%s)\n", up ? "UP" : "DOWN", why);
}

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
bool isDuplicate(const NodeMsg &m) {
  static struct { uint8_t node; uint16_t boot; uint32_t seq; } seen[32];
  static int head = 0;
  for (auto &s : seen)
    if (s.seq == m.seq && s.boot == m.boot_id && s.node == m.node_id) return true;
  seen[head] = {m.node_id, m.boot_id, m.seq};
  head = (head + 1) % 32;
  return false;
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

void handleDrinkLocked(const NodeMsg &m, DrinkEvent ev, float amount) {
  if (ev == EV_DRINK) {
    S.hist[S.histHead].ms = millis();
    S.hist[S.histHead].ml = amount;
    S.histHead = (S.histHead + 1) % 32;
    if (S.histCount < 32) S.histCount++;
    S.lastDrinkMs = millis();
    S.mlToday += amount;
    S.holdUntilMs = millis() + scaledMinToMs(HOLD_DOWN_MIN);
    setSignLocked(false, "ดื่มแล้ว");
  }
  switch (ev) {
    case EV_DRINK:       Serial.printf("[DRINK] ดื่ม %.0f ml  (วันนี้ %.0f ml)\n", amount, S.mlToday); break;
    case EV_REFILL:      Serial.printf("[DRINK] เติมน้ำ +%.0f g\n", amount); break;
    case EV_CUP_PLACED:  Serial.printf("[DRINK] วางแก้ว w=%.0f g (รอนิ่ง ~10 วิ แล้วค่อยเทียบ)\n", m.weight_g); break;
    case EV_CUP_LIFTED:  Serial.println("[DRINK] ยกแก้วขึ้น (ยังไม่นับ รอวางกลับ)"); break;
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
    if (isDuplicate(m)) { Lock l; S.dupCount++; continue; }

    UpItem up = {};
    JsonDocument rec;
    DrinkEvent ev;
    float amount = 0;
    {
      Lock l;
      S.rxCount++;
      S.lastDistCm = m.dist_cm;
      float refBefore = S.drink.refWeight();
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
      rec["estop"]     = (bool)estop;
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
      doc["ml"]      = amount;
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
      if (haveTime && S.todayYday != now.tm_yday) {      // วันใหม่ → รีเซ็ตยอดวันนี้
        if (S.todayYday != -1) Serial.println("[DRINK] วันใหม่ รีเซ็ตยอดวันนี้");
        S.todayYday = now.tm_yday;
        S.mlToday = 0;
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
  bool near = S.lastDistCm >= 5 && S.lastDistCm < PRESENCE_CM;
  x[0] = min(minsSince(S.lastDrinkMs), 240.0f);
  x[1] = mlLastHourLocked();
  x[2] = S.mlToday;
  x[3] = isnan(S.roomT)  ? AI_MEAN[3] : S.roomT;    // DHT ยังไม่มีค่า ใช้ค่าเฉลี่ยแทน
  x[4] = isnan(S.roomRH) ? AI_MEAN[4] : S.roomRH;
  x[5] = (pir || near) ? 1 : 0;
  x[6] = S.soundP2P;
}

void printAILocked() {
  Serial.printf("[AI] p=%.2f sign=%s |", S.aiP, S.signUp ? "UP" : "DOWN");
  for (int i = 0; i < AI_N_FEATURES; i++) Serial.printf(" %s=%.1f", AI_FEATURE_NAMES[i], S.aiX[i]);
  Serial.println();
}

void runAIOnce(bool print) {
  LineMsg lm;
  bool sendLine = false;
  {
    Lock l;
    computeFeaturesLocked(S.aiX);
    S.aiP = aiPredict(S.aiX);
    bool holding = (int32_t)(S.holdUntilMs - millis()) > 0;
    if (!S.signUp && !holding && S.aiP >= AI_P_UP) setSignLocked(true, "AI: เสี่ยงดื่มน้อย");
    else if (S.signUp && S.aiP <= AI_P_DOWN)        setSignLocked(false, "AI: ความเสี่ยงลดลง");

    // ป้ายยกค้างนานแล้วยังไม่ดื่ม → LINE
    if (S.signUp && minsSince(S.signUpSinceMs) >= LINE_AFTER_MIN &&
        (S.lastLineMs == 0 || millis() - S.lastLineMs >= LINE_COOLDOWN_MS)) {
      snprintf(lm.text, sizeof(lm.text), "ถึงเวลาดื่มน้ำแล้ว 💧 ไม่ได้ดื่มมา %.0f นาที วันนี้ดื่มไป %.0f ml",
               minsSince(S.lastDrinkMs), S.mlToday);
      S.lastLineMs = millis();
      sendLine = true;
    }
    if (print) printAILocked();
  }
  if (sendLine) xQueueSend(lineQueue, &lm, 0);
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
      String resp;
      int code = lineBroadcast(LINE_TOKEN, lm.text, &resp);
      Serial.printf("[LINE] broadcast -> %d\n", code);
      if (code != 200 && resp.length()) Serial.println(resp);
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
      LineMsg lm;
      strlcpy(lm.text, "ถึงเวลาดื่มน้ำแล้ว 💧 (ทดสอบ)", sizeof(lm.text));
      xQueueSend(lineQueue, &lm, 0);
    } else if (c == 'q') {
      int usage = lineConsumption(LINE_TOKEN);
      if (usage >= 0) Serial.printf("LINE messages used this month: %d\n", usage);
      else Serial.println("Failed to read LINE message usage");
    } else if (c == 'a') { runAIOnce(true); }
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

  rxQueue     = xQueueCreate(10, sizeof(RxItem));
  uploadQueue = xQueueCreate(6, sizeof(UpItem));
  servoQueue  = xQueueCreate(1, sizeof(int));
  lineQueue   = xQueueCreate(2, sizeof(LineMsg));
  stateMutex  = xSemaphoreCreateMutex();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
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
  xTaskCreate(aiTask,     "ai",     4096,  nullptr, 2, nullptr);
  xTaskCreate(netTask,    "net",    12288, nullptr, 1, nullptr);   // HTTPS/TLS ใช้ stack เยอะ

  Serial.printf("Gateway ready  (AI model: %s, DEMO_SPEED=%d)\n", AI_TRAINED_ON, DEMO_SPEED);
  Serial.println("Serial: a=ดูค่า AI  s=สถานะ  u/n=ยก/ลดป้าย  c=ปลด e-stop  m=ส่ง LINE ทดสอบ  q=โควตา LINE");
}

void loop() {
  handleSerialCommands();
  vTaskDelay(pdMS_TO_TICKS(20));
}
