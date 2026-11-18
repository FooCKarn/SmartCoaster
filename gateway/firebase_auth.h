// gateway/firebase_auth.h — gateway ล็อกอิน Firebase Auth (email/password) ผ่าน REST (E5)
//
// 1) signInWithPassword → ได้ idToken (อายุ 1 ชม.) + refreshToken
// 2) ทุกคำขอไป RTDB แนบ ?auth=<idToken>  → rules ตรวจได้ว่าเป็น uid ของ gateway
// 3) ก่อน idToken หมดอายุ 5 นาที ขอใหม่ด้วย refreshToken (ไม่ต้องส่งรหัสผ่านซ้ำ)
// ใช้จาก netTask เท่านั้น
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "root_ca.h"

#ifndef FB_API_KEY
#define FB_API_KEY  ""
#endif
#ifndef FB_EMAIL
#define FB_EMAIL    ""     // ว่าง = ไม่ล็อกอิน (ใช้ได้เฉพาะ rules แบบ test mode)
#endif
#ifndef FB_PASSWORD
#define FB_PASSWORD ""
#endif

inline bool fbAuthEnabled() { return strlen(FB_EMAIL) > 0; }

struct FbAuth {
  String idToken, refreshToken, uid;
  uint32_t expiresAtMs = 0;
  uint32_t lastTryMs = 0;
} fbAuth;

void secureClient(WiFiClientSecure &c) {
#ifdef SC_ROOT_CA
  c.setCACert(SC_ROOT_CA);
#else
  c.setInsecure();
#endif
}

// POST แล้วอ่าน JSON ตอบกลับ คืน HTTP code
int authPost(const String &url, const char *contentType, const String &body, JsonDocument &out) {
  WiFiClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, url)) return -2;
  http.addHeader("Content-Type", contentType);
  int code = http.POST(body);
  if (code > 0) deserializeJson(out, http.getString());
  http.end();
  return code;
}

bool fbSignIn() {
  JsonDocument req, res;
  req["email"] = FB_EMAIL;
  req["password"] = FB_PASSWORD;
  req["returnSecureToken"] = true;
  String body; serializeJson(req, body);
  int code = authPost(String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FB_API_KEY,
                      "application/json", body, res);
  if (code != 200) {
    Serial.printf("[AUTH] sign-in FAILED %d %s\n", code, (const char *)(res["error"]["message"] | ""));
    return false;
  }
  fbAuth.idToken      = res["idToken"].as<String>();
  fbAuth.refreshToken = res["refreshToken"].as<String>();
  fbAuth.uid          = res["localId"].as<String>();
  fbAuth.expiresAtMs  = millis() + (uint32_t)(res["expiresIn"].as<String>().toInt()) * 1000UL;
  Serial.printf("[AUTH] signed in  uid=%s  <- ใส่ uid นี้ใน firebase/database.rules.json\n", fbAuth.uid.c_str());
  return true;
}

bool fbRefresh() {
  JsonDocument res;
  int code = authPost(String("https://securetoken.googleapis.com/v1/token?key=") + FB_API_KEY,
                      "application/x-www-form-urlencoded",
                      "grant_type=refresh_token&refresh_token=" + fbAuth.refreshToken, res);
  if (code != 200) { Serial.printf("[AUTH] refresh FAILED %d\n", code); return false; }
  fbAuth.idToken      = res["id_token"].as<String>();
  fbAuth.refreshToken = res["refresh_token"].as<String>();
  fbAuth.expiresAtMs  = millis() + (uint32_t)(res["expires_in"].as<String>().toInt()) * 1000UL;
  Serial.println("[AUTH] token refreshed");
  return true;
}

// คืน token ที่ยังใช้ได้ ("" = ยังไม่มี) เรียกก่อนทุกคำขอ
String fbToken() {
  if (!fbAuthEnabled()) return "";
  bool fresh = fbAuth.idToken.length() && (int32_t)(fbAuth.expiresAtMs - millis()) > 5 * 60 * 1000;
  if (fresh) return fbAuth.idToken;
  if (millis() - fbAuth.lastTryMs < 15000 && fbAuth.lastTryMs) return fbAuth.idToken;  // เพิ่งลองไป รอก่อน
  fbAuth.lastTryMs = millis();
  bool ok = fbAuth.refreshToken.length() ? fbRefresh() : false;
  if (!ok) ok = fbSignIn();
  return ok ? fbAuth.idToken : "";
}

// RTDB ตอบ 401 → token เสีย ล้างแล้วล็อกอินใหม่รอบหน้า
void fbTokenRejected() {
  fbAuth.idToken = "";
  fbAuth.refreshToken = "";
  fbAuth.lastTryMs = 0;
}
