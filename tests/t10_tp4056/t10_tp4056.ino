// tests/t10_tp4056/t10_tp4056.ino
// TP4056 + แบต 18650: อ่านแรงดันแบตผ่านตัวแบ่ง 100k/100k ที่ GPIO34 (ESP32 ใช้ไฟ USB)
const int VBAT_PIN = 34;          // ADC1_CH6 (input-only) ใช้ได้แม้เปิด Wi-Fi/ESP-NOW
const float DIVIDER = 2.0f;       // 100k/100k → แรงดันที่ขา = VBAT ÷ 2
const float VBAT_GAIN = 1.000f;   // ปรับให้ตรงมัลติมิเตอร์: ค่ามัลติมิเตอร์ ÷ ค่าที่อ่านได้

uint32_t readPinMv() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {  // เฉลี่ย 16 ครั้ง ลดสัญญาณรบกวน
    sum += analogReadMilliVolts(VBAT_PIN);
    delay(2);
  }
  return sum / 16;
}

int percentOf(float v) {          // % โดยประมาณของ Li-ion 1 เซลล์ (ตอนไม่มีโหลดหนัก)
  const float V[] = {3.00, 3.30, 3.60, 3.70, 3.80, 3.90, 4.00, 4.10, 4.20};
  const int   P[] = {   0,    5,   20,   40,   60,   75,   85,   95,  100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v < V[i]) return P[i - 1] + (P[i] - P[i - 1]) * (v - V[i - 1]) / (V[i] - V[i - 1]);
  return 100;
}

const char *stateText(float v) {
  if (v < 0.5f)  return "ไม่มีแรงดัน - เช็คสาย OUT+ และตัวต้านทานตัวบน";
  if (v < 3.0f)  return "ต่ำมาก - แบตหมดหรือวงจรป้องกันตัดไฟ";
  if (v > 4.25f) return "สูงเกิน - เช็คค่าตัวต้านทาน (ต้อง 100k ทั้งคู่)";
  return "ok";
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);   // อ่านได้ถึงประมาณ 3.1 V (แบต 4.2 V → ขาเห็น 2.1 V)
  Serial.println("t10 TP4056 + battery sense (GPIO34)");
}

void loop() {
  uint32_t pinMv = readPinMv();
  float vbat = pinMv * DIVIDER * VBAT_GAIN / 1000.0f;

  static float ref = vbat;                // แรงดันเมื่อ 30 วินาทีก่อน
  static uint32_t refMs = millis();
  static float d30 = 0;                   // บวก = กำลังชาร์จ, ลบ = กำลังคายประจุ
  if (millis() - refMs >= 30000) {
    d30 = vbat - ref;
    ref = vbat;
    refMs = millis();
  }

  Serial.printf("pin = %lu mV  VBAT = %.2f V  ~%d %%  d30s = %+.3f V  [%s]\n",
                (unsigned long)pinMv, vbat, percentOf(vbat), d30, stateText(vbat));
  delay(1000);
}
