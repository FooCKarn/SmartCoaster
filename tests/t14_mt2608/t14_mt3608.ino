// tests/t14_mt3608/t14_mt3608.ino
// MT3608 boost: แบต 18650 → MT3608 → +5 V  อ่านแรงดันเข้า/ออกผ่านตัวแบ่ง 100k/100k (ESP32 ใช้ไฟ USB)
//   VIN  (VBAT) → ตัวแบ่ง → GPIO34
//   VOUT (+5 V) → ตัวแบ่ง → GPIO33   (5 V ÷ 2 = 2.5 V ที่ขา อยู่ในช่วงที่ ADC อ่านได้)
//
// !! ปรับ trimmer ให้ VOUT = 5.0 V ด้วยมัลติมิเตอร์ "ก่อน" ต่อสายตัวแบ่งเข้า D33 !!
// !! MT3608 ที่ยังไม่ปรับอาจออกสูงถึง ~28 V ซึ่งทำให้ ESP32 พัง                      !!
//
// คำสั่งทาง Serial Monitor (115200): b = จำค่า VOUT ตอนไม่มีโหลดเป็น baseline แล้วแสดง "ตก" เทียบ baseline
const int VIN_PIN  = 34;          // ADC1_CH6 (input-only)
const int VOUT_PIN = 33;          // ADC1_CH5
const float DIVIDER = 2.0f;       // 100k/100k
const float VIN_GAIN  = 1.000f;   // ปรับให้ตรงมัลติมิเตอร์: ค่ามัลติมิเตอร์ ÷ ค่าที่อ่านได้
const float VOUT_GAIN = 1.000f;

float baseline = 0;               // VOUT ตอนไม่มีโหลด (0 = ยังไม่ได้ตั้ง)

uint32_t readPinMv(int pin) {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {  // เฉลี่ย 16 ครั้ง ลดสัญญาณรบกวน
    sum += analogReadMilliVolts(pin);
    delay(2);
  }
  return sum / 16;
}

// อ่านเร็ว 200 ครั้งติดกัน เอา max − min ที่ขา แล้วคูณตัวแบ่ง = สวิงของ VOUT (หยาบ)
// ADC อ่านริปเปิลความถี่สวิตช์ของ MT3608 (~1 MHz) ไม่ทัน ได้แค่การแกว่งช้าๆ ใช้ออสซิลโลสโคปถ้าอยากดูจริง
float swingMv() {
  uint32_t lo = UINT32_MAX, hi = 0;
  for (int i = 0; i < 200; i++) {
    uint32_t v = analogReadMilliVolts(VOUT_PIN);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return (hi - lo) * DIVIDER;
}

const char *voutText(float v) {
  if (v < 0.5f)  return "ไม่มีแรงดัน - เช็คสาย VOUT / ตัวแบ่ง / แบตหมด";
  if (v < 4.5f)  return "ต่ำ - ปรับ trimmer ให้ได้ 5.0 V";
  if (v <= 5.25f) return "ok";
  if (v < 6.0f)  return "สูง - ปรับ trimmer ลง (HC-SR04 ทนได้ไม่เกิน ~5.5 V)";
  return "อันตราย - ถอดสายตัวแบ่ง D33 ทันที (ขา ADC ทนได้ไม่เกิน 3.3 V → VOUT ต้องไม่เกิน 6.6 V)";
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(VIN_PIN,  ADC_11db);   // ขาเห็นได้ถึงประมาณ 3.1 V
  analogSetPinAttenuation(VOUT_PIN, ADC_11db);
  Serial.println("t14 MT3608 boost (VIN = GPIO34, VOUT = GPIO33)");
  Serial.println("ส่ง b เพื่อจำค่า VOUT ตอนไม่มีโหลด แล้วต่อโหลด (เช่น R 100 Ω) ดูว่าตกเท่าไร");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b') {
      baseline = readPinMv(VOUT_PIN) * DIVIDER * VOUT_GAIN / 1000.0f;
      Serial.printf("baseline (ไม่มีโหลด) = %.3f V\n", baseline);
    }
    while (Serial.available()) Serial.read();
  }

  float vin  = readPinMv(VIN_PIN)  * DIVIDER * VIN_GAIN  / 1000.0f;
  float vout = readPinMv(VOUT_PIN) * DIVIDER * VOUT_GAIN / 1000.0f;
  float swing = swingMv();

  Serial.printf("VIN = %.2f V  VOUT = %.3f V  สวิง = %.0f mV", vin, vout, swing);
  if (baseline > 0) Serial.printf("  ตกจาก baseline = %+.3f V", vout - baseline);
  Serial.printf("  [%s]", voutText(vout));
  if (vin > 0.5f && vout < vin + 0.3f) Serial.print("  (VOUT ไม่สูงกว่า VIN: บูสต์ไม่ทำงานหรือ trimmer ต่ำสุด)");
  if (vin > 0.5f && vin < 3.0f)        Serial.print("  (แบตต่ำ <3.0 V)");
  Serial.println();
  delay(500);
}
