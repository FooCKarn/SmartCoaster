// gateway/drink_detect.h — ตรวจจับการดื่มจากน้ำหนักแก้ว (ไม่ใช้ไลบรารี Arduino ทดสอบบนคอมได้)
//
// หลักการ: จำ "น้ำหนักตอนนิ่ง" ล่าสุดของแก้วไว้ (ref)
//   - น้ำหนักนิ่งใหม่ น้อยกว่า ref ≥ drinkMinG  → ดื่มไป (ref - ใหม่) ml   (น้ำ 1 g ≈ 1 ml)
//   - น้ำหนักนิ่งใหม่ มากกว่า ref ≥ refillMinG → เติมน้ำ
//   - ยกแก้วขึ้น (น้ำหนัก < cupMinG) ไม่นับเป็นการดื่ม แค่รอจนวางกลับแล้วนิ่ง ค่อยเทียบ
//   - จิบเล็ก ๆ หลายครั้งที่แต่ละครั้ง < drinkMinG จะถูกรวมจนถึงเกณฑ์แล้วนับทีเดียว
#pragma once
#include <math.h>

struct DrinkConfig {
  float cupMinG     = 40;    // ต่ำกว่านี้ = ไม่มีแก้วบนแท่น
  float stableG     = 5;     // สองค่าติดกันห่างไม่เกินนี้ = นิ่ง
  int   stableCount = 2;     // ต้องนิ่งกี่คู่ติดกัน (node ส่งทุก 5 วิ → ~10 วิ)
  float drinkMinG   = 15;    // ลดลงอย่างน้อยเท่านี้ = ดื่ม
  float drinkMaxG   = 600;   // ลดลงเกินนี้ = เปลี่ยนแก้ว ไม่นับ
  float refillMinG  = 30;    // เพิ่มขึ้นอย่างน้อยเท่านี้ = เติมน้ำ
};

enum DrinkEvent { EV_NONE = 0, EV_DRINK, EV_REFILL, EV_CUP_LIFTED, EV_CUP_PLACED, EV_CUP_CHANGED };

static inline const char *drinkEventName(DrinkEvent e) {
  switch (e) {
    case EV_DRINK:       return "drink";
    case EV_REFILL:      return "refill";
    case EV_CUP_LIFTED:  return "lifted";
    case EV_CUP_PLACED:  return "placed";
    case EV_CUP_CHANGED: return "cup_changed";
    default:             return "none";
  }
}

class DrinkDetector {
 public:
  DrinkConfig cfg;

  // ใส่น้ำหนักทีละค่า (ตามลำดับเวลา) คืนเหตุการณ์ และปริมาณ (g) ใน *amountG
  DrinkEvent update(float w, float *amountG) {
    if (amountG) *amountG = 0;
    if (isnan(w)) return EV_NONE;                 // HX711 ไม่ตอบ ข้ามไป

    if (w < cfg.cupMinG) {                        // ไม่มีแก้ว
      bool was = present_;
      present_ = false; run_ = 0; last_ = w;
      return was ? EV_CUP_LIFTED : EV_NONE;
    }

    DrinkEvent ev = EV_NONE;
    if (!present_) { present_ = true; run_ = 0; last_ = NAN; ev = EV_CUP_PLACED; }

    if (!isnan(last_) && fabsf(w - last_) <= cfg.stableG) run_++;
    else run_ = 0;
    float prev = last_;
    last_ = w;
    if (run_ < cfg.stableCount) return ev;        // ยังไม่นิ่ง

    float level = (w + prev) / 2;
    if (isnan(ref_)) { ref_ = level; return ev; } // แก้วแรก

    float d = ref_ - level;                       // + = ลดลง
    if (d >= cfg.drinkMinG && d <= cfg.drinkMaxG) {
      ref_ = level;
      if (amountG) *amountG = d;
      return EV_DRINK;
    }
    if (d > cfg.drinkMaxG) {
      ref_ = level;
      return EV_CUP_CHANGED;
    }
    if (-d >= cfg.refillMinG) {
      ref_ = level;
      if (amountG) *amountG = -d;
      return EV_REFILL;
    }
    if (fabsf(d) < cfg.stableG) ref_ = level;     // ตามค่าเลื่อนช้า ๆ ของ load cell
    return ev;
  }

  bool  cupPresent() const { return present_; }
  float refWeight()  const { return ref_; }

 private:
  bool  present_ = false;
  int   run_ = 0;
  float last_ = NAN;
  float ref_ = NAN;
};
