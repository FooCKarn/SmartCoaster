// gateway/line_notify.h — ตัดสินใจ "เมื่อไหร่ / ระดับไหน / ข้อความอะไร" จะส่ง LINE ตามที่ AI ประเมิน
// ไม่ใช้ไลบรารี Arduino (ทดสอบบนคอมได้ เหมือน drink_detect.h)
//
// เวลาทั้งหมดในไฟล์นี้เป็น "นาทีในระบบ" = millis()/60000 * DEMO_SPEED (gateway.ino คำนวณให้ผ่าน nowMin)
//
// กติกา
//   - ป้ายต้องยกอยู่ (AI/กฎตายตัวสั่งยก) และไม่ใช่ช่วงเวลาเงียบ
//   - ครั้งแรก: ป้ายยกค้าง >= firstAfterMin นาที  ·  ครั้งต่อไป: ห่างจากครั้งก่อน >= repeatAfterMin
//   - ทุกข้อความห่างกันอย่างน้อย minGapMin และไม่เกิน maxPerDay ข้อความ/วัน (กันโควตา LINE หมด)
//   - ระดับ URGENT เมื่อไม่ได้ดื่มนานมาก หรือ AI ประเมินความเสี่ยงสูงมาก  ไม่งั้น GENTLE
//   - นับโควตา/คูลดาวน์เฉพาะข้อความที่ "ส่งสำเร็จ"  ส่งพลาด → ลองใหม่ได้ แต่ไม่เกิน maxRetry ครั้ง
//   - ระหว่างรอส่ง (pending) ไม่สร้างข้อความซ้ำ
//   - แจ้งทันทีตอนยกธง (decideRaise): ข้อความสั้น 1-2 บรรทัดบอกเหตุผลของ AI ส่งครั้งเดียวต่อการยก 1 ครั้ง
//     นับรวมในเพดานต่อวัน แต่ไม่กระทบจังหวะ "เตือนหลังยกค้าง firstAfterMin" ด้านบน
//     ห่างจากข้อความแจ้งทันทีครั้งก่อนอย่างน้อย minGapMin (กันป้ายยกลงสลับแล้วข้อความรัว)
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

enum LineLevel : uint8_t { LN_NONE = 0, LN_GENTLE = 1, LN_URGENT = 2 };

struct LineNotifyConfig {
  float firstAfterMin   = 10;    // ป้ายยกค้างกี่นาที ถึงเตือนครั้งแรก
  float repeatAfterMin  = 30;    // ยังไม่ดื่ม → เตือนซ้ำห่างกันกี่นาที
  float minGapMin       = 20;    // ห่างกันอย่างน้อย (ทุกระดับ)
  float urgentNoDrinkMin = 90;   // ไม่ได้ดื่มนานเท่านี้ → URGENT
  float urgentP         = 0.85f; // AI ประเมินความเสี่ยงเท่านี้ขึ้นไป → URGENT
  int   maxPerDay       = 6;     // เพดานข้อความอัตโนมัติต่อวัน
  float retryAfterMin   = 1;     // ส่งพลาด ลองใหม่หลังกี่นาที
  int   maxRetry        = 3;     // ลองใหม่ได้กี่ครั้งต่อ 1 ข้อความ
  float needMlPerHour   = 100;   // ตรงกับ DEFAULT_NEED_ML ใน ai/train.py
};

// ค่าที่ gateway ส่งเข้ามาทุกรอบ AI (10 วินาที)
struct LineNotifyInput {
  bool  signUp = false;
  bool  quiet = false;
  float p = NAN;               // ความเสี่ยงจาก AI 0..1
  float minsSinceDrink = 0;
  float minsSignUp = 0;        // ป้ายยกมากี่นาทีแล้ว
  float mlLastHour = 0;
  float mlToday = 0;
  float roomT = NAN;
  int   dayId = -1;            // เปลี่ยนเมื่อขึ้นวันใหม่ (tm_yday) -1 = ยังไม่รู้เวลา
};

// ข้อมูลสำหรับข้อความ "ยกธงทันที": เหตุผลของ AI
// x[] และ c[] เรียงตาม feature ของโมเดล (ai_model.h): 0 mins_since_drink, 1 ml_last_hour, 2 ml_today,
//   3 room_t, 4 room_rh, 5 present, 6 sound_p2p
// c[i] = ส่วนที่ feature นั้นดันความเสี่ยง (+ = ดันให้ยกธง) = W[i] * (x[i] - mean[i]) / std[i]
struct LineRaiseInput {
  float p = NAN;                 // ความเสี่ยงจาก AI 0..1
  float minsSinceDrink = 0;
  bool  overdue = false;         // ยกเพราะกฎตายตัว "ไม่ได้ดื่มนานเกินกำหนด" (ไม่ใช่ AI)
  float overdueMin = 90;
  const float *x = nullptr;
  const float *c = nullptr;
};

class LineNotifier {
 public:
  LineNotifyConfig cfg;

  // เพิ่งยกธงในรอบนี้ → ควรแจ้งทันทีไหม (true = ให้สร้างข้อความแล้วเรียก markRaiseQueued)
  bool decideRaise(const LineNotifyInput &in, float nowMin) {
    rollDay(in);
    if (!in.signUp || in.quiet) return false;
    if (pending_) return false;
    if (sentToday_ >= cfg.maxPerDay) return false;
    if (hasRaised_ && nowMin - lastRaiseMin_ < cfg.minGapMin) return false;
    return true;
  }
  void markRaiseQueued(float nowMin) { pending_ = true; pendingRaise_ = true; lastTryMin_ = nowMin; }

  // คืนระดับที่ควรส่งตอนนี้ (LN_NONE = ยังไม่ต้องส่ง)
  LineLevel decide(const LineNotifyInput &in, float nowMin) {
    rollDay(in);
    if (!in.signUp || in.quiet) {                      // ดื่มแล้ว / ป้ายลง / กลางคืน → จบรอบเตือนนี้
      sentInEpisode_ = 0; failCount_ = 0;
      return LN_NONE;
    }
    if (pending_) return LN_NONE;                      // มีข้อความรอส่งอยู่
    if (sentToday_ >= cfg.maxPerDay) return LN_NONE;
    if (hasSent_ && nowMin - lastSentMin_ < cfg.minGapMin) return LN_NONE;

    bool due;
    if (failCount_ > 0)            due = nowMin - lastTryMin_ >= cfg.retryAfterMin;   // กำลังลองใหม่
    else if (sentInEpisode_ == 0)  due = in.minsSignUp >= cfg.firstAfterMin;
    else                           due = nowMin - lastSentMin_ >= cfg.repeatAfterMin;
    if (!due) return LN_NONE;

    bool urgent = in.minsSinceDrink >= cfg.urgentNoDrinkMin || (!isnan(in.p) && in.p >= cfg.urgentP);
    return urgent ? LN_URGENT : LN_GENTLE;
  }

  void markQueued(float nowMin) { pending_ = true; lastTryMin_ = nowMin; }

  // netTask รายงานผลหลังส่ง
  void onResult(bool ok, float nowMin) {
    pending_ = false;
    if (pendingRaise_) {                               // ข้อความแจ้งทันทีตอนยกธง: ไม่ลองใหม่ (ไม่กระทบรอบเตือนปกติ)
      pendingRaise_ = false;
      hasRaised_ = true; lastRaiseMin_ = nowMin;
      if (ok) sentToday_++;
      return;
    }
    if (ok) {
      sentInEpisode_++; sentToday_++; hasSent_ = true; lastSentMin_ = nowMin; failCount_ = 0;
    } else if (++failCount_ >= cfg.maxRetry) {         // ลองครบแล้ว เว้นช่วงไปก่อน (ไม่นับโควตา)
      failCount_ = 0; sentInEpisode_++; hasSent_ = true; lastSentMin_ = nowMin;
    }
  }
  // ข้อความค้างคิวแต่ผู้ใช้ดื่มไปแล้ว / ป้ายลงแล้ว → ยกเลิก ไม่นับ ไม่ส่ง
  void onCancel() {
    if (pendingRaise_) { pendingRaise_ = false; pending_ = false; return; }
    pending_ = false; failCount_ = 0;
  }

  int  sentToday() const { return sentToday_; }
  int  sentInEpisode() const { return sentInEpisode_; }
  bool pending() const { return pending_; }

 private:
  void rollDay(const LineNotifyInput &in) {
    if (in.dayId != -1 && in.dayId != day_) {          // วันใหม่ → รีเซ็ตยอดนับ
      day_ = in.dayId; sentToday_ = 0;
    }
  }
  bool  pending_ = false, hasSent_ = false, pendingRaise_ = false, hasRaised_ = false;
  int   sentInEpisode_ = 0, sentToday_ = 0, failCount_ = 0, day_ = -1;
  float lastSentMin_ = 0, lastTryMin_ = 0, lastRaiseMin_ = 0;
};

// ปริมาณที่แนะนำให้ดื่มตอนนี้ (ml ปัดเป็นสิบ) = ที่ควรดื่มต่อชม. − ที่ดื่มไปแล้วใน 1 ชม. จำกัด 80–250
// ห้องร้อนต้องการมากขึ้น (สูตรเดียวกับ need_ml() ใน ai/train.py)
static inline int lineSuggestMl(float mlLastHour, float roomT, float base = 100) {
  float t = isnan(roomT) ? 26.0f : roomT;
  float need = base + 10.0f * fmaxf(0.0f, t - 28.0f);
  float s = need - mlLastHour;
  if (s < 80) s = 80;
  if (s > 250) s = 250;
  return (int)(roundf(s / 10.0f) * 10.0f);
}

// นาที → ข้อความอ่านง่าย: "45 นาที", "2 ชม. 5 นาที", "มากกว่า 1 วัน" (กันตัวเลขอย่าง 2600 นาที)
static inline const char *lnMins(float m, char *buf, size_t n) {
  if (!isfinite(m) || m < 0) m = 0;
  if (m >= 24 * 60)  snprintf(buf, n, "มากกว่า 1 วัน");
  else if (m >= 120) snprintf(buf, n, "%d ชม. %d นาที", (int)m / 60, (int)m % 60);
  else               snprintf(buf, n, "%.0f นาที", m);
  return buf;
}

// ข้อความหนึ่งบรรทัดต่อท้าย: ถ้าใส่ไม่ครบจะข้ามทั้งบรรทัด (ไม่ตัดกลางตัวอักษรไทย 3 ไบต์/อีโมจิ 4 ไบต์ → LINE ไม่ reject)
#define LN_APPEND(...) do { int k_ = snprintf(out + len, n - len, __VA_ARGS__); \
  if (k_ < 0 || (size_t)k_ >= n - len) out[len] = 0; else len += k_; } while (0)

// สร้างข้อความ LINE (UTF-8) ใน out ขนาด n ไบต์ (แนะนำ 480: ไทยตัวละ 3 ไบต์)
static inline void lineBuildText(char *out, size_t n, LineLevel lv, const LineNotifyInput &in,
                                 int repeatNo /* 0 = ครั้งแรกของรอบนี้ */) {
  int sug = lineSuggestMl(in.mlLastHour, in.roomT);
  int pct = isnan(in.p) ? -1 : (int)lroundf(in.p * 100.0f);
  size_t len = 0;
  out[0] = 0;
  char mt[40]; lnMins(in.minsSinceDrink, mt, sizeof(mt));
  if (lv == LN_URGENT) {
    LN_APPEND("⚠️ ยังไม่ได้ดื่มน้ำมานาน %s แล้ว\n"
              "วันนี้ดื่มไป %.0f ml · ลองดื่มสัก ~%d ml ตอนนี้เลย",
              mt, in.mlToday, sug);
  } else if (repeatNo > 0) {
    LN_APPEND("💧 เตือนอีกครั้ง ยังไม่ได้ดื่มมา %s\n"
              "ลองดื่มสัก ~%d ml นะ (วันนี้ %.0f ml)",
              mt, sug, in.mlToday);
  } else {
    LN_APPEND("💧 ถึงเวลาดื่มน้ำแล้ว\n"
              "ไม่ได้ดื่มน้ำมา %s · ชั่วโมงที่ผ่านมา %.0f ml · วันนี้ %.0f ml\n"
              "ลองดื่มน้ำสัก ~%d ml นะ",
              mt, in.mlLastHour, in.mlToday, sug);
  }
  if (!isnan(in.roomT) && in.roomT >= 29.0f) LN_APPEND("\n อุณหภูมิห้องสูง %.0f°C ควรดื่มน้ำเพิ่ม", in.roomT);
  if (pct >= 0)                              LN_APPEND("\n AI ประเมินเสี่ยงดื่มน้ำน้อย %d%%", pct);
}

// ข้อความตอนยกธง: สั้น 1-2 บรรทัด บอกว่า "ทำไมถึงยก" (feature ที่ดันความเสี่ยงมากที่สุด 2 อันดับแรก)
// บรรทัด 1 = ยกธงแล้ว (+ AI เสี่ยงกี่ %)   บรรทัด 2 = เพราะอะไร
static inline void lineBuildRaiseText(char *out, size_t n, const LineRaiseInput &in) {
  int pct = isnan(in.p) ? -1 : (int)lroundf(in.p * 100.0f);
  size_t len = 0;
  out[0] = 0;
  if (in.overdue) {
    char mt[40]; lnMins(in.minsSinceDrink, mt, sizeof(mt));
    LN_APPEND("🚩 ยกธงแล้ว: ไม่ได้ดื่มน้ำมา %s (เกิน %.0f นาที)", mt, in.overdueMin);
    if (pct >= 0) LN_APPEND("\nAI ประเมินเสี่ยงดื่มน้ำน้อย %d%%", pct);
    return;
  }
  if (pct >= 0) LN_APPEND("🚩 ยกธงเตือนให้ดื่มน้ำ · AI ประเมินเสี่ยง %d%%", pct);
  else          LN_APPEND("🚩 ยกธงเตือนให้ดื่มน้ำ");

  // เลือก feature ที่ดันขึ้นมากสุด 2 ตัว (เฉพาะที่ดัน > 0.1)
  int top[2] = {-1, -1};
  if (in.c) {
    for (int k = 0; k < 2; k++) {
      float best = 0.1f;
      for (int i = 0; i < 7; i++) {
        if (i == top[0]) continue;
        if (in.c[i] > best) { best = in.c[i]; top[k] = i; }
      }
    }
  }
  char why[160]; size_t wl = 0; why[0] = 0;
  for (int k = 0; k < 2 && top[k] >= 0; k++) {
    float v = in.x ? in.x[top[k]] : 0;
    const char *sep = wl ? ", " : "";
    int r = -1;
    switch (top[k]) {
      case 0: { char mt[40]; r = snprintf(why + wl, sizeof(why) - wl, "%sไม่ได้ดื่มมา %s", sep, lnMins(v, mt, sizeof(mt))); } break;
      case 1: r = snprintf(why + wl, sizeof(why) - wl, "%sชม.ที่ผ่านมาดื่มแค่ %.0f ml", sep, v); break;
      case 2: r = snprintf(why + wl, sizeof(why) - wl, "%sวันนี้ดื่มแค่ %.0f ml", sep, v); break;
      case 3: r = snprintf(why + wl, sizeof(why) - wl, "%sห้องร้อน %.0f°C", sep, v); break;
      case 4: r = snprintf(why + wl, sizeof(why) - wl, "%sอากาศแห้ง (ชื้น %.0f%%)", sep, v); break;
      case 5: r = snprintf(why + wl, sizeof(why) - wl, "%sไม่เห็นคนอยู่หน้าโต๊ะ", sep); break;
      case 6: r = snprintf(why + wl, sizeof(why) - wl, "%sห้องเสียงดัง (มักลืมดื่ม)", sep); break;
    }
    if (r < 0 || (size_t)r >= sizeof(why) - wl) { why[wl] = 0; break; }   // ใส่ไม่ครบ ข้ามทั้งส่วน
    wl += r;
  }
  if (wl) LN_APPEND("\nเพราะ: %s", why);
  else    LN_APPEND("\nเหตุผล: ความเสี่ยงโดยรวมสูงกว่าเกณฑ์");
}
