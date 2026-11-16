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

const uint8_t NODE_ID = 1;
const int HX_DT = 19, HX_SCK = 18, TRIG = 25, ECHO = 26;
const uint32_t SEND_EVERY_MS = 5000;

HX711 scale;
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

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  scale.begin(HX_DT, HX_SCK);
  scale.set_scale(CAL_FACTOR);
  delay(2000);
  scale.tare(20);

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
  static uint32_t last = 0;
  if (millis() - last < SEND_EVERY_MS) return;
  last = millis();

  msg.version  = MSG_VERSION;
  msg.node_id  = NODE_ID;
  msg.seq      = ++seq;
  msg.weight_g = scale.get_units(5);
  msg.dist_cm  = readCm();
  msg.batt_mv  = 0;

  esp_err_t r = esp_now_send(GATEWAY_MAC, (uint8_t *)&msg, sizeof(msg));
  Serial.printf("seq=%lu  w=%.1f g  d=%.1f cm  send=%s  (ok=%lu fail=%lu)\n",
                msg.seq, msg.weight_g, msg.dist_cm, r == ESP_OK ? "queued" : "ERR",
                okCount, failCount);
}
