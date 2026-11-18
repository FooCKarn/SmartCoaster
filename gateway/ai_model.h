// gateway/ai_model.h — สร้างอัตโนมัติโดย ai/train.py (อย่าแก้มือ ให้รัน train.py ใหม่)
// โมเดล: logistic regression ทำนายความเสี่ยงว่า 60 นาทีข้างหน้าจะดื่มน้ำไม่พอ
// ข้อมูลที่ใช้เทรน: office_hydration_v1.csv  (39099 ตัวอย่าง, test accuracy = 0.624)
#pragma once
#include <math.h>

#define AI_N_FEATURES 7
static const char *const AI_FEATURE_NAMES[AI_N_FEATURES] = {"mins_since_drink", "ml_last_hour", "ml_today", "room_t", "room_rh", "present", "sound_p2p"};
static const float AI_MEAN[AI_N_FEATURES] = {60.8911f, 86.0967f, 398.473f, 26.9827f, 57.65f, 0.789483f, 199.336f};
static const float AI_STD[AI_N_FEATURES]  = {65.146f, 108.074f, 358.888f, 1.131f, 4.69467f, 0.407676f, 139.19f};
static const float AI_W[AI_N_FEATURES]    = {0.203784f, -0.144021f, -0.131574f, 0.0680776f, -0.0203652f, -0.173234f, 0.0965572f};
static const float AI_B = 0.640812f;
static const char *const AI_TRAINED_ON = "office_hydration_v1.csv";

// คืนค่าความเสี่ยง 0..1 (x ต้องเรียงตาม AI_FEATURE_NAMES)
static inline float aiPredict(const float *x) {
  float z = AI_B;
  for (int i = 0; i < AI_N_FEATURES; i++) z += AI_W[i] * (x[i] - AI_MEAN[i]) / AI_STD[i];
  return 1.0f / (1.0f + expf(-z));
}
