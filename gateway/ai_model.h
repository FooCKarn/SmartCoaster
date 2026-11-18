// gateway/ai_model.h — สร้างอัตโนมัติโดย ai/train.py (อย่าแก้มือ ให้รัน train.py ใหม่)
// โมเดล: logistic regression ทำนายความเสี่ยงว่า 60 นาทีข้างหน้าจะดื่มน้ำไม่พอ
// ข้อมูลที่ใช้เทรน: synthetic 60 days  (6336 ตัวอย่าง, test accuracy = 0.708)
#pragma once
#include <math.h>

#define AI_N_FEATURES 7
static const char *const AI_FEATURE_NAMES[AI_N_FEATURES] = {"mins_since_drink", "ml_last_hour", "ml_today", "room_t", "room_rh", "present", "sound_p2p"};
static const float AI_MEAN[AI_N_FEATURES] = {61.673f, 154.297f, 946.754f, 30.4334f, 64.0543f, 0.822601f, 311.786f};
static const float AI_STD[AI_N_FEATURES]  = {66.4579f, 176.162f, 730.577f, 2.51177f, 10.4913f, 0.382006f, 161.475f};
static const float AI_W[AI_N_FEATURES]    = {0.135625f, -0.213787f, 0.259618f, -0.0841736f, 0.0688374f, -0.380572f, 0.207127f};
static const float AI_B = 0.564682f;
static const char *const AI_TRAINED_ON = "synthetic 60 days";

// คืนค่าความเสี่ยง 0..1 (x ต้องเรียงตาม AI_FEATURE_NAMES)
static inline float aiPredict(const float *x) {
  float z = AI_B;
  for (int i = 0; i < AI_N_FEATURES; i++) z += AI_W[i] * (x[i] - AI_MEAN[i]) / AI_STD[i];
  return 1.0f / (1.0f + expf(-z));
}
