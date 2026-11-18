// gateway/gateway.ino
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include "msg.h"
#include "line_push.h"
#include "drink_detect.h"
#include "ai_model.h"
#include "secrets.h"

const int DHT_PIN = 14, PIR_PIN = 27, SND_AO = 35, SND_DO = 32;
const int SERVO_PIN = 18, STOP_BTN = 33, LED = 2;

// ===== ตั้งค่า AI / ป้าย / LINE =====
// DEMO_SPEED: 1 = เวลาจริง, 60 = เร่งเวลา 60 เท่า (1 วินาที = 1 นาที) ไว้ถ่ายวิดีโอ
// ตอนเร่งเวลา ข้อมูลที่ส่งขึ้น Firebase จะติด demo:true และ ai/train.py จะไม่เอาไปเทรน
#ifndef DEMO_SPEED
#define DEMO_SPEED 1
#endif
const float    AI_P_UP        = 0.60f;  // ความเสี่ยง ≥ นี้ → ยกป้าย
const float    AI_P_DOWN      = 0.52f;  // ความเสี่ยง ≤ นี้ → ลดป้าย (ห่างกันไว้กันป้ายกระพือ)
const float    HOLD_DOWN_MIN  = 15;     // ดื่มแล้ว ป้ายลงค้างอย่างน้อยกี่นาที
const float    LINE_AFTER_MIN = 10;     // ป้ายยกค้างกี่นาทีแล้วยังไม่ดื่ม → ส่ง LINE
const uint32_t LINE_COOLDOWN_MS = 30UL * 60UL * 1000UL;  // ส่ง LINE ห่างกันอย่างน้อย 30 นาที (เวลาจริง กันโควตาหมด)
const float    PRESENCE_CM    = 80;     // ultrasonic < นี้ = มีคนนั่งหน้าแก้ว
const uint32_t PIR_WINDOW_MS  = 5UL * 60UL * 1000UL;     // PIR ขยับภายใน 5 นาที = มีคน
const int      SIGN_UP_DEG = 90, SIGN_DOWN_DEG = 0;
const uint32_t AI_EVERY_MS = 10000;     // คำนวณ AI ทุก 10 วินาที
// =====================================

DHT dht(DHT_PIN, DHT11);

// ---- ข้อมูลจาก node (เขียนใน callback, อ่านใน loop) ----
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool rxReady = false;
NodeMsg rxMsg;
int rxRssi = 0;

// ---- interrupt ----
uint32_t motionCount = 0;
volatile uint32_t lastMotionMs = 0;
volatile bool estop = false;
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR onMotion() {
  portENTER_CRITICAL_ISR(&motionMux);
  motionCount++;
  lastMotionMs = millis();
  portEXIT_CRITICAL_ISR(&motionMux);
}
void IRAM_ATTR onStop()   { estop = true; }

uint32_t readMotionCount() {
  portENTER_CRITICAL(&motionMux);
  uint32_t count = motionCount;
  portEXIT_CRITICAL(&motionMux);
  return count;
}

// ---- ค่าเซนเซอร์ gateway ล่าสุด ----
float roomT = NAN, roomRH = NAN;
int soundP2P = 0;
float lastDistCm = -1;

// ---- การดื่ม ----
DrinkDetector drink;
struct DrinkLog { uint32_t ms; float ml; };
DrinkLog drinkHist[32];
int drinkHead = 0, drinkCount = 0;
uint32_t lastDrinkMs = 0;      // millis() ตอนดื่มครั้งล่าสุด (เริ่มนับจากเปิดเครื่อง)
float mlToday = 0;
int todayYday = -1;

// ---- AI + ป้าย ----
float aiX[AI_N_FEATURES];
float aiP = NAN;
bool signUp = false;
uint32_t signUpSinceMs = 0, holdUntilMs = 0;
uint32_t lastLineMs = 0;
bool needReminder = false;
String reminderText = "ถึงเวลาดื่มน้ำแล้ว 💧";

// นาทีที่ผ่านไป (คูณ DEMO_SPEED แล้ว)
float minsSince(uint32_t ms) { return (millis() - ms) / 60000.0f * DEMO_SPEED; }
uint32_t scaledMinToMs(float m) { return (uint32_t)(m * 60000.0f / DEMO_SPEED); }

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(NodeMsg)) return;
  portENTER_CRITICAL(&rxMux);
  memcpy(&rxMsg, data, sizeof(NodeMsg));
  rxRssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  rxReady = true;
  portEXIT_CRITICAL(&rxMux);
}

void servoDeg(int deg) {
  uint32_t us = map(constrain(deg, 0, 180), 0, 180, 500, 2400);
  ledcWrite(SERVO_PIN, us * 65535UL / 20000UL);
}

void setSign(bool up, const char *why) {
  if (estop) return;                       // หยุดฉุกเฉินอยู่ ห้ามขยับ
  if (up == signUp) return;
  signUp = up;
  if (up) signUpSinceMs = millis();
  servoDeg(up ? SIGN_UP_DEG : SIGN_DOWN_DEG);
  Serial.printf("[SIGN] %s (%s)\n", up ? "UP ⬆" : "DOWN ⬇", why);
}

int readSound() {
  int lo = 4095, hi = 0; uint32_t t0 = millis();
  while (millis() - t0 < 50) { int v = analogRead(SND_AO); lo = min(lo, v); hi = max(hi, v); }
  return hi - lo;
}

// ---------------- Firebase ----------------
int firebaseSend(const char *method, const String &path, const String &body) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();   // ช่วง checkpoint เท่านั้น! ขั้น 5.5 เปลี่ยนเป็นตรวจ cert (E5)
  HTTPClient http;
  String auth = String(DB_AUTH).length() ? String("?auth=") + DB_AUTH : "";
  http.begin(client, String(DB_URL) + path + ".json" + auth);
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, body);
  http.end();
  return code;
}

bool postToFirebase(const NodeMsg &m, int rssi) {
  JsonDocument doc;
  doc["node_id"]   = m.node_id;
  doc["seq"]       = m.seq;
  doc["weight_g"]  = m.weight_g;
  doc["dist_cm"]   = m.dist_cm;
  doc["batt_mv"]   = m.batt_mv;
  doc["rssi"]      = rssi;
  doc["room_t"]    = roomT;
  doc["room_rh"]   = roomRH;
  doc["sound_p2p"] = soundP2P;
  doc["motion_count"] = readMotionCount();
  // ---- ผลการดื่ม + AI ----
  doc["cup"]       = drink.cupPresent();
  doc["ml_today"]  = mlToday;
  doc["mins_since_drink"] = minsSince(lastDrinkMs);
  if (!isnan(aiP)) {
    doc["ai_p"] = aiP;
    JsonArray x = doc["ai_x"].to<JsonArray>();     // feature ที่ AI ใช้ เก็บไว้เทรนใหม่ (ai/train.py)
    for (int i = 0; i < AI_N_FEATURES; i++) x.add(aiX[i]);
  }
  doc["sign_up"]   = signUp;
  doc["estop"]     = (bool)estop;
  if (DEMO_SPEED != 1) doc["demo"] = true;
  doc["ts"][".sv"] = "timestamp";          // ให้ Firebase ใส่เวลาเอง (ms)
  String body; serializeJson(doc, body);

  int code = firebaseSend("POST", "/readings", body);
  firebaseSend("PUT", "/latest", body);
  Serial.printf("Firebase POST -> %d\n", code);
  return code == 200;
}

void postDrinkEvent(DrinkEvent ev, float amount, uint8_t nodeId) {
  JsonDocument doc;
  doc["node_id"] = nodeId;
  doc["type"]    = drinkEventName(ev);
  doc["ml"]      = amount;
  doc["ml_today"] = mlToday;
  if (DEMO_SPEED != 1) doc["demo"] = true;
  doc["ts"][".sv"] = "timestamp";
  String body; serializeJson(doc, body);
  // ดื่ม → /drinks (ข้อมูลให้ AI), เติมน้ำ → /refills
  int code = firebaseSend("POST", ev == EV_DRINK ? "/drinks" : "/refills", body);
  Serial.printf("Firebase %s -> %d\n", ev == EV_DRINK ? "/drinks" : "/refills", code);
}

// ---------------- การดื่ม ----------------
void checkNewDay() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;        // ยังไม่ได้เวลาจาก NTP
  if (todayYday != t.tm_yday) {
    if (todayYday != -1) Serial.println("[DRINK] วันใหม่ รีเซ็ตยอดวันนี้");
    todayYday = t.tm_yday;
    mlToday = 0;
  }
}

void recordDrink(float ml) {
  drinkHist[drinkHead] = {(uint32_t)millis(), ml};
  drinkHead = (drinkHead + 1) % 32;
  if (drinkCount < 32) drinkCount++;
  lastDrinkMs = millis();
  mlToday += ml;
}

float mlLastHour() {
  float sum = 0;
  for (int i = 0; i < drinkCount; i++) {
    const DrinkLog &d = drinkHist[(drinkHead - 1 - i + 32) % 32];
    if (minsSince(d.ms) < 60) sum += d.ml;
  }
  return sum;
}

void handleWeight(const NodeMsg &m) {
  float amount;
  DrinkEvent ev = drink.update(m.weight_g, &amount);
  if (ev == EV_NONE) return;
  Serial.printf("[DRINK] %s %.0f g (วันนี้ %.0f ml)\n", drinkEventName(ev), amount, mlToday + (ev == EV_DRINK ? amount : 0));
  if (ev == EV_DRINK) {
    recordDrink(amount);
    holdUntilMs = millis() + scaledMinToMs(HOLD_DOWN_MIN);
    setSign(false, "ดื่มแล้ว");
  }
  if (ev == EV_DRINK || ev == EV_REFILL) postDrinkEvent(ev, amount, m.node_id);
}

// ---------------- AI ----------------
bool personPresent() {
  bool pir = lastMotionMs != 0 && millis() - lastMotionMs < PIR_WINDOW_MS;
  bool near = lastDistCm > 0 && lastDistCm < PRESENCE_CM;
  return pir || near;
}

// ลำดับต้องตรงกับ FEATURES ใน ai/train.py
void computeFeatures(float *x) {
  x[0] = min(minsSince(lastDrinkMs), 240.0f);
  x[1] = mlLastHour();
  x[2] = mlToday;
  x[3] = isnan(roomT)  ? AI_MEAN[3] : roomT;    // DHT ยังไม่มีค่า ใช้ค่าเฉลี่ยแทน
  x[4] = isnan(roomRH) ? AI_MEAN[4] : roomRH;
  x[5] = personPresent() ? 1 : 0;
  x[6] = soundP2P;
}

void runAI() {
  computeFeatures(aiX);
  aiP = aiPredict(aiX);
  bool holding = (int32_t)(holdUntilMs - millis()) > 0;
  if (!signUp && !holding && aiP >= AI_P_UP) setSign(true, "AI: เสี่ยงดื่มน้อย");
  else if (signUp && aiP <= AI_P_DOWN)       setSign(false, "AI: ความเสี่ยงลดลง");

  // ป้ายยกค้างนานแล้วยังไม่ดื่ม → LINE
  if (signUp && minsSince(signUpSinceMs) >= LINE_AFTER_MIN && !needReminder &&
      (lastLineMs == 0 || millis() - lastLineMs >= LINE_COOLDOWN_MS)) {
    char buf[160];
    snprintf(buf, sizeof(buf), "ถึงเวลาดื่มน้ำแล้ว 💧 ไม่ได้ดื่มมา %.0f นาที วันนี้ดื่มไป %.0f ml",
             minsSince(lastDrinkMs), mlToday);
    reminderText = buf;
    needReminder = true;
  }
}

void printAI() {
  Serial.printf("[AI] p=%.2f  sign=%s  (", aiP, signUp ? "UP" : "DOWN");
  for (int i = 0; i < AI_N_FEATURES; i++) Serial.printf("%s=%.1f ", AI_FEATURE_NAMES[i], aiX[i]);
  Serial.printf(") model=%s\n", AI_TRAINED_ON);
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'm') {
      reminderText = "ถึงเวลาดื่มน้ำแล้ว 💧 (ทดสอบ)";
      needReminder = true;
    } else if (c == 'q') {
      int usage = lineConsumption(LINE_TOKEN);
      if (usage >= 0) Serial.printf("LINE messages used this month: %d\n", usage);
      else Serial.println("Failed to read LINE message usage");
    } else if (c == 'a') { runAI(); printAI(); }
    else if (c == 'u') { setSign(true, "สั่งจาก Serial"); }
    else if (c == 'n') { setSign(false, "สั่งจาก Serial"); }
    else if (c == 'c') { estop = false; Serial.println("[ESTOP] ปลดหยุดฉุกเฉินแล้ว"); servoDeg(signUp ? SIGN_UP_DEG : SIGN_DOWN_DEG); }
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
  servoDeg(SIGN_DOWN_DEG);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.printf("\nWiFi OK  IP=%s  WiFi channel = %ld  <- ใส่เลขนี้ใน node\n",
                WiFi.localIP().toString().c_str(), WiFi.channel());
  configTzTime("ICT-7", "pool.ntp.org", "time.google.com");   // เวลาไทย ใช้รีเซ็ตยอดดื่มตอนเที่ยงคืน

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); while (true) delay(1000); }
  esp_now_register_recv_cb(onRecv);
  Serial.printf("Gateway ready  (AI model: %s, DEMO_SPEED=%d)\n", AI_TRAINED_ON, DEMO_SPEED);
  Serial.println("Serial: a=ดูค่า AI  u/n=ยก/ลดป้าย  c=ปลด e-stop  m=ส่ง LINE ทดสอบ  q=โควตา LINE");
}

void loop() {
  handleSerialCommands();

  if (needReminder) {
    String response;
    int code = lineBroadcast(LINE_TOKEN, reminderText.c_str(), &response);
    Serial.printf("LINE broadcast -> %d\n", code);
    if (code != 200 && response.length()) Serial.println(response);
    lastLineMs = millis();                 // สำเร็จหรือไม่ก็พักก่อน กันยิงซ้ำรัว ๆ
    needReminder = false;
  }

  // 1) ปุ่มหยุดฉุกเฉิน: ตัดสัญญาณเซอร์โว (กด c ใน Serial เพื่อปลด)
  if (estop) { ledcWrite(SERVO_PIN, 0); }

  // 2) อ่านเซนเซอร์ gateway ทุก 5 วินาที
  static uint32_t lastSense = 0;
  if (millis() - lastSense > 5000) {
    lastSense = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) roomT = t;
    if (!isnan(h)) roomRH = h;
    soundP2P = readSound();
    checkNewDay();
  }

  // 3) AI ตัดสินใจยก/ลดป้าย
  static uint32_t lastAI = 0;
  if (millis() - lastAI > AI_EVERY_MS) {
    lastAI = millis();
    runAI();
    printAI();
  }

  // 4) มีข้อมูลจาก node → ตรวจการดื่ม → อัปโหลด
  if (rxReady) {
    NodeMsg m; int rssi;
    portENTER_CRITICAL(&rxMux);
    m = rxMsg; rssi = rxRssi; rxReady = false;
    portEXIT_CRITICAL(&rxMux);

    lastDistCm = m.dist_cm;
    Serial.printf("RX node=%d seq=%lu w=%.1f d=%.1f rssi=%d | T=%.0f RH=%.0f snd=%d mot=%lu\n",
                  m.node_id, m.seq, m.weight_g, m.dist_cm, rssi,
                  roomT, roomRH, soundP2P, readMotionCount());
    handleWeight(m);
    digitalWrite(LED, HIGH);
    postToFirebase(m, rssi);
    digitalWrite(LED, LOW);
  }

  // 5) Wi-Fi หลุด → ต่อใหม่ (Robustness)
  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi lost, reconnecting"); WiFi.reconnect(); }
  }
}
