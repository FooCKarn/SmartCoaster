// common/msg.h  — ใช้ไฟล์เดียวกันทั้ง node และ gateway
// แก้ที่นี่แล้วคัดลอกไปทับ node/msg.h และ gateway/msg.h (Arduino IDE ไม่ include ไฟล์นอกโฟลเดอร์ sketch)
#pragma once
#include <stdint.h>

#define MSG_VERSION 2

enum : uint8_t {
  MSG_DATA = 1,   // node → gateway: ค่าที่วัดได้
  MSG_ACK  = 2,   // gateway → node: "ได้รับ seq นี้แล้ว" (E4 ACK ระดับแอป)
};

typedef struct __attribute__((packed)) {
  uint8_t  version;     // = MSG_VERSION
  uint8_t  type;        // = MSG_DATA
  uint8_t  node_id;     // 1, 2, ... (คะแนน Scalability ต้องมี node ID)
  uint16_t boot_id;     // สุ่มใหม่ทุกครั้งที่ node เปิด ใช้กับ seq กันข้อมูลซ้ำ
  uint32_t seq;         // เลขลำดับ ใช้วัด packet delivery rate
  uint32_t age_ms;      // ค่านี้วัดมาแล้วกี่ ms (> 0 เมื่อเป็นค่าที่ค้างในคิวแล้วส่งซ้ำ)
  uint8_t  queued;      // node ยังมีค่าค้างในคิวรอส่งอีกกี่ค่า
  float    weight_g;    // น้ำหนักบนแท่น (g), NaN = HX711 ไม่ตอบ
  float    dist_cm;     // ระยะจาก ultrasonic (-1 = ไม่มี echo)
  uint16_t batt_mv;     // แรงดันแบต (0 = ยังไม่วัด)
} NodeMsg;              // 25 ไบต์

typedef struct __attribute__((packed)) {
  uint8_t  version;     // = MSG_VERSION
  uint8_t  type;        // = MSG_ACK
  uint8_t  node_id;
  uint16_t boot_id;
  uint32_t seq;         // seq ที่ได้รับแล้ว
} AckMsg;               // 9 ไบต์
