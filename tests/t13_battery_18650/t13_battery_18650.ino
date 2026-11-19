// tests/t13_battery_18650/t13_battery_18650.ino
// ถ่าน 18650 1 ก้อน: อ่านแรงดันตรงจากขั้วแบต ผ่านตัวแบ่ง 100k/100k ที่ GPIO34
// ไม่มี TP4056 / LDO / MT3608 ในเทสนี้ — ESP32 ใช้ไฟ USB อย่างเดียว (ห้ามต่อแบตเข้า 3V3 หรือ VIN)
//
// คำสั่งทาง Serial Monitor (115200, ส่งแบบ "New Line"):
//   g<ค่ามัลติมิเตอร์>   เช่น g3.852  → คำนวณ VBAT_GAIN ให้ (นำไปใส่ใน t10 / t14 ด้วย)
//   r                    → ล้างสถิติ min/max ของหน้าต่าง 10 วินาที
const int VBAT_PIN = 34;          // ADC1_CH6 (input-only) ใช้ได้แม้เปิด Wi-Fi/ESP-NOW
const float DIVIDER = 2.0f;       // 100k/100k → แรงดันที่ขา = VBAT ÷ 2
float vbatGain = 1.000f;          // ปรับให้ตรงมัลติมิเตอร์ (ตั้งอัตโนมัติด้วยคำสั่ง g)

const int WINDOW = 10;            // สถิติ min/max/เฉลี่ย ทุก 10 ค่า (= 10 วินาที)
float wMin = 99, wMax = 0, wSum = 0;
int wN = 0;

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
  if (v < 0.5f)  return "ไม่มีแรงดัน - เช็คสาย B+ / B− / ตัวต้านทานตัวบน";
  if (v < 2.8f)  return "ต่ำมาก - แบตหมดลึก ห้ามใช้ต่อ";
  if (v < 3.3f)  return "ต่ำ - ควรชาร์จ";
  if (v > 4.25f) return "สูงเกิน - เช็คค่าตัวต้านทาน (ต้อง 100k ทั้งคู่)";
  return "ok";
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'r') {
    wMin = 99; wMax = 0; wSum = 0; wN = 0;
    Serial.println("ล้างสถิติแล้ว");
  } else if (c == 'g') {
    float meter = Serial.parseFloat();             // ค่าจากมัลติมิเตอร์ (V)
    float raw = readPinMv() * DIVIDER / 1000.0f;   // ค่าที่ ESP32 อ่านได้ก่อนปรับ
    if (raw > 0.5f && meter > 2.5f && meter < 4.3f) {
      vbatGain = meter / raw;
      Serial.printf("ตั้ง VBAT_GAIN = %.4f (ใช้ค่านี้ในโค้ด t10 / t14 ด้วย)\n", vbatGain);
    } else {
      Serial.println("ใช้ไม่ได้ - ส่งแบบ g3.852 (ค่ามัลติมิเตอร์ 2.5–4.3 V) และต้องมีแรงดันที่ขา");
    }
  }
  while (Serial.available()) Serial.read();        // ทิ้ง newline ที่เหลือ
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);     // อ่านได้ถึงประมาณ 3.1 V (แบต 4.2 V → ขาเห็น 2.1 V)
  Serial.println("t13 18650 single cell (GPIO34)");
  Serial.println("ส่ง g<ค่ามัลติมิเตอร์> เพื่อปรับค่า · ส่ง r เพื่อล้างสถิติ");
}

void loop() {
  handleSerial();

  uint32_t pinMv = readPinMv();
  float vbat = pinMv * DIVIDER * vbatGain / 1000.0f;

  if (vbat < wMin) wMin = vbat;
  if (vbat > wMax) wMax = vbat;
  wSum += vbat;
  wN++;

  Serial.printf("pin = %lu mV  VBAT = %.3f V  ~%d %%  [%s]\n",
                (unsigned long)pinMv, vbat, percentOf(vbat), stateText(vbat));

  if (wN >= WINDOW) {
    float spreadMv = (wMax - wMin) * 1000.0f;
    Serial.printf("--- 10 s: min = %.3f  max = %.3f  avg = %.3f V  แกว่ง = %.0f mV %s\n",
                  wMin, wMax, wSum / wN, spreadMv,
                  spreadMv > 30 ? "(แกว่งมาก - เช็คสายหลวม)" : "(นิ่งดี)");
    wMin = 99; wMax = 0; wSum = 0; wN = 0;
  }
  delay(1000);
}
