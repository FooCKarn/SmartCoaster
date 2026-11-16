// common/line_push.h  (canonical copy)
// Copy to tests/t10_line/line_push.h and gateway/line_push.h whenever you edit it
// (Arduino IDE cannot include files outside the sketch folder - same rule as msg.h).
//
// Needs: ESP32 Arduino core 3.x, ArduinoJson v7, Wi-Fi already connected.
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Send one text message with the LINE Messaging API (push).
// Returns the HTTP status code (200 = OK), or -1 if Wi-Fi is not connected,
// or a negative HTTPClient error code if the request failed.
// NOTE: every successful push counts against the monthly free quota.
inline int linePush(const char* token, const char* userId, const char* text,
                    String* resp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  JsonDocument doc;
  doc["to"] = userId;
  JsonObject m = doc["messages"].add<JsonObject>();
  m["type"] = "text";
  m["text"] = text;
  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();  // development only - use client.setCACert(...) before deploying

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, "https://api.line.me/v2/bot/message/push")) return -2;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + token);
  int code = http.POST(body);
  if (resp && code > 0) *resp = http.getString();
  http.end();
  return code;
}

// Messages used this month (GET /v2/bot/message/quota/consumption).
// Returns totalUsage, or -1 on any failure.
inline int lineConsumption(const char* token) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  WiFiClientSecure client;
  client.setInsecure();  // development only

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, "https://api.line.me/v2/bot/message/quota/consumption")) return -1;
  http.addHeader("Authorization", String("Bearer ") + token);
  int code = http.GET();
  int usage = -1;
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) usage = doc["totalUsage"] | -1;
  }
  http.end();
  return usage;
}