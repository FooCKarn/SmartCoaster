// tests/t10_line/t10_line.ino
// Standalone test: can the ESP32 broadcast a LINE message? (run on the GATEWAY board)
// Serial Monitor 115200:  s = send test message   q = show quota used this month
#include <WiFi.h>
#include "secrets.h"     // WIFI_SSID, WIFI_PASS, LINE_TOKEN
#include "line_push.h"

uint32_t lastSendMs = 0;

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting Wi-Fi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi OK  channel=%ld  IP=%s\n", WiFi.channel(),
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("Wi-Fi FAILED (2.4 GHz only - check SSID/password)");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  connectWifi();
  Serial.println("Type 's' = send LINE test message, 'q' = quota used this month");
  Serial.println("(broadcasts may use the channel's message quota)");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    if (c == 's') {
      if (lastSendMs && millis() - lastSendMs < 5000) {
        Serial.println("wait 5 s between sends");
      } else {
        String resp;
        int code = lineBroadcast(LINE_TOKEN,
                                 "SmartCoaster test: ถึงเวลาดื่มน้ำแล้ว 💧", &resp);
        Serial.printf("LINE broadcast -> %d\n", code);
        if (code != 200) Serial.println(resp);   // LINE explains the error here
        if (code == 200) lastSendMs = millis();
      }
    }

    if (c == 'q') {
      Serial.printf("Messages used this month: %d\n", lineConsumption(LINE_TOKEN));
    }
  }

  if (WiFi.status() != WL_CONNECTED) {            // simple auto-reconnect
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}
