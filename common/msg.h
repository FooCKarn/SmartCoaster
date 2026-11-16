// common/msg.h  — ใช้ไฟล์เดียวกันทั้ง node และ gateway
#pragma once
#include <stdint.h>

#define MSG_VERSION 1

typedef struct __attribute__((packed)) {
  uint8_t  version;     // = MSG_VERSION
  uint8_t  node_id;     // 1, 2, ... (คะแนน Scalability ต้องมี node ID)
  uint32_t seq;         // เลขลำดับ ใช้วัด packet delivery rate
  float    weight_g;    // น้ำหนักบนแท่น (g)
  float    dist_cm;     // ระยะจาก ultrasonic (-1 = ไม่มี echo)
  uint16_t batt_mv;     // แรงดันแบต (0 = ยังไม่วัด, ระยะ 6)
} NodeMsg;              // 16 ไบต์
