#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Send a JSON POST to a LINE Messaging API endpoint and return its HTTP code.
inline int lineSend(const char* url, const char* token, const String& body,
                    String* resp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[LINE] ส่งไม่ได้: WiFi ไม่ได้ต่อ"); return -1; }
  WiFiClientSecure client;
#ifdef SC_ROOT_CA
  client.setCACert(SC_ROOT_CA);   // E5: ตรวจ cert (gateway/root_ca.h)
#else
  client.setInsecure();           // development only
#endif
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, url)) return -2;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + token);
  int code = http.POST(body);
  if (resp && code > 0) *resp = http.getString();
  if (code < 0) {
    // บอกสาเหตุให้ชัด: ต่อไม่ติด / TLS (cert, เวลา) / timeout
    char tls[120] = "";
    int tlsErr = client.lastError(tls, sizeof(tls));
    Serial.printf("[LINE] ส่งไม่ได้: %s", HTTPClient::errorToString(code).c_str());
    if (tlsErr) Serial.printf("  TLS: %s (%d)", tls, tlsErr);
    Serial.printf("  heap=%lu  time=%s\n", (unsigned long)ESP.getFreeHeap(),
                  time(nullptr) > 1700000000 ? "ok" : "ยังไม่ได้ NTP");
  }
  http.end();
  return code;
}

// Broadcast a text message to all friends of the bot; the request has no "to" field.
inline int lineBroadcast(const char* token, const char* text, String* resp = nullptr) {
  JsonDocument doc;
  JsonObject m = doc["messages"].add<JsonObject>();
  m["type"] = "text";
  m["text"] = text;
  String body;
  serializeJson(doc, body);
  return lineSend("https://api.line.me/v2/bot/message/broadcast", token, body, resp);
}

// Send one text message to a specific user.
inline int linePush(const char* token, const char* userId, const char* text,
                    String* resp = nullptr) {
  JsonDocument doc;
  doc["to"] = userId;
  JsonObject m = doc["messages"].add<JsonObject>();
  m["type"] = "text";
  m["text"] = text;
  String body;
  serializeJson(doc, body);
  return lineSend("https://api.line.me/v2/bot/message/push", token, body, resp);
}

// Messages used this month (GET /v2/bot/message/quota/consumption).
// Returns totalUsage, or -1 on any failure.
inline int lineConsumption(const char* token) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  WiFiClientSecure client;
#ifdef SC_ROOT_CA
  client.setCACert(SC_ROOT_CA);
#else
  client.setInsecure();  // development only
#endif

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
