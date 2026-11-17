// gateway/gateway.ino
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "msg.h"
#include "line_push.h"
#include "secrets.h"

const int DHT_PIN = 14, PIR_PIN = 27, SND_AO = 35, SND_DO = 32;
const int SERVO_PIN = 18, STOP_BTN = 33, LED = 2;

DHT dht(DHT_PIN, DHT11);

// ---- ข้อมูลจาก node (เขียนใน callback, อ่านใน loop) ----
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool rxReady = false;
NodeMsg rxMsg;
int rxRssi = 0;

// ---- interrupt ----
uint32_t motionCount = 0;
volatile bool estop = false;
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR onMotion() {
  portENTER_CRITICAL_ISR(&motionMux);
  motionCount++;
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

// Manual test trigger until automatic drink detection is implemented.
const uint32_t LINE_COOLDOWN_MS = 30UL * 60UL * 1000UL;
uint32_t lastLineMs = 0;
bool needReminder = false;

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

int readSound() {
  int lo = 4095, hi = 0; uint32_t t0 = millis();
  while (millis() - t0 < 50) { int v = analogRead(SND_AO); lo = min(lo, v); hi = max(hi, v); }
  return hi - lo;
}

bool postToFirebase(const NodeMsg &m, int rssi) {
  if (WiFi.status() != WL_CONNECTED) return false;
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
  doc["ts"][".sv"] = "timestamp";          // ให้ Firebase ใส่เวลาเอง (ms)
  String body; serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();   // ช่วง checkpoint เท่านั้น! ขั้น 5.5 เปลี่ยนเป็นตรวจ cert (E5)
  HTTPClient http;
  String auth = String(DB_AUTH).length() ? String("?auth=") + DB_AUTH : "";

  http.begin(client, String(DB_URL) + "/readings.json" + auth);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();

  http.begin(client, String(DB_URL) + "/latest.json" + auth);
  http.addHeader("Content-Type", "application/json");
  http.PUT(body);
  http.end();

  Serial.printf("Firebase POST -> %d\n", code);
  return code == 200;
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'm') {
      needReminder = true;
    } else if (command == 'q') {
      int usage = lineConsumption(LINE_TOKEN);
      if (usage >= 0) Serial.printf("LINE messages used this month: %d\n", usage);
      else Serial.println("Failed to read LINE message usage");
    }
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
  servoDeg(0);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.printf("\nWiFi OK  IP=%s  WiFi channel = %ld  <- ใส่เลขนี้ใน node\n",
                WiFi.localIP().toString().c_str(), WiFi.channel());

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); while (true) delay(1000); }
  esp_now_register_recv_cb(onRecv);
  Serial.println("Gateway ready");
  Serial.println("Serial: m = broadcast MOCK LINE reminder, q = show LINE usage");
}

void loop() {
  handleSerialCommands();

  if (needReminder &&
      (lastLineMs == 0 || millis() - lastLineMs >= LINE_COOLDOWN_MS)) {
    String response;
    int code = lineBroadcast(LINE_TOKEN, "ถึงเวลาดื่มน้ำแล้ว 💧", &response);
    Serial.printf("LINE broadcast -> %d\n", code);
    if (code != 200 && response.length()) Serial.println(response);
    if (code == 200) lastLineMs = millis();
    needReminder = false;
  }

  // 1) ปุ่มหยุดฉุกเฉิน
  if (estop) { ledcWrite(SERVO_PIN, 0); }

  // 2) อ่านเซนเซอร์ gateway ทุก 5 วินาที
  static uint32_t lastSense = 0;
  if (millis() - lastSense > 5000) {
    lastSense = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) roomT = t;
    if (!isnan(h)) roomRH = h;
    soundP2P = readSound();
  }

  // 3) มีข้อมูลจาก node → อัปโหลด
  if (rxReady) {
    NodeMsg m; int rssi;
    portENTER_CRITICAL(&rxMux);
    m = rxMsg; rssi = rxRssi; rxReady = false;
    portEXIT_CRITICAL(&rxMux);

    Serial.printf("RX node=%d seq=%lu w=%.1f d=%.1f rssi=%d | T=%.0f RH=%.0f snd=%d mot=%lu\n",
                  m.node_id, m.seq, m.weight_g, m.dist_cm, rssi,
                  roomT, roomRH, soundP2P, readMotionCount());
    digitalWrite(LED, HIGH);
    postToFirebase(m, rssi);
    digitalWrite(LED, LOW);
  }

  // 4) Wi-Fi หลุด → ต่อใหม่ (Robustness)
  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi lost, reconnecting"); WiFi.reconnect(); }
  }
}
