// tests/t11_ldo_3v3/t11_ldo_3v3.ino
// LDO 3.3 V: ESP32 กินไฟจากแบต → TP4056 → LDO → ขา 3V3 (ไม่เสียบ USB)
// ตอนรันจากแบตไม่มี Serial ให้ดู จึงจดผลลง flash (NVS) แล้วค่อยเสียบ USB มาอ่านทีหลัง
#include <WiFi.h>
#include <Preferences.h>

const int LED = 2;                      // LED บนบอร์ด
const int VBAT_PIN = 34;                // ตัวแบ่ง 100k/100k จาก t10 (ไม่ต่อก็ได้)
const float DIVIDER = 2.0f;
const uint32_t SCAN_EVERY_MS = 5000;    // สแกน Wi-Fi ทุก 5 วินาที = ดึงกระแสสูงเป็นช่วงๆ ทดสอบว่า LDO จ่ายทัน
const uint32_t SAVE_EVERY_MS = 10000;   // จดเวลาที่รันได้ลง flash ทุก 10 วินาที

Preferences prefs;
uint32_t brownouts = 0;                 // จำนวนครั้งที่รีเซ็ตเพราะไฟตก (สะสมจนกว่าจะส่ง c)
uint32_t minVbatMv = UINT32_MAX;        // VBAT ต่ำสุดของรอบนี้
bool scanning = false;

uint32_t readVbatMv() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(VBAT_PIN);
  return sum / 16 * DIVIDER;
}

const char *resetText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (เพิ่งจ่ายไฟ)";
    case ESP_RST_BROWNOUT: return "BROWNOUT (ไฟตก)";
    case ESP_RST_EXT:      return "EXT (ปุ่ม EN)";
    case ESP_RST_SW:       return "SW (รีเซ็ตจากโค้ด)";
    case ESP_RST_PANIC:    return "PANIC (โปรแกรมล่ม)";
    default:               return "อื่นๆ";
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);

  // อ่านผลของรอบก่อน (รอบที่รันจาก LDO) แล้วเริ่มนับรอบใหม่
  esp_reset_reason_t reason = esp_reset_reason();
  prefs.begin("t11", false);
  brownouts = prefs.getUInt("brown", 0) + (reason == ESP_RST_BROWNOUT ? 1 : 0);
  uint32_t prevUpS = prefs.getUInt("upS", 0);
  uint32_t prevMinMv = prefs.getUInt("minV", 0);
  prefs.putUInt("brown", brownouts);
  prefs.putUInt("upS", 0);
  prefs.putUInt("minV", 0);

  delay(1000);                          // รอ Serial Monitor
  Serial.println("t11 LDO 3.3 V test");
  Serial.printf("reset = %s\n", resetText(reason));
  Serial.printf("รอบก่อนรันได้ %lu s\n", (unsigned long)prevUpS);
  Serial.printf("brownout สะสม = %lu\n", (unsigned long)brownouts);
  Serial.printf("VBAT ต่ำสุดรอบก่อน = %lu mV (0 = ไม่ได้ต่อตัวแบ่ง)\n", (unsigned long)prevMinMv);
  Serial.println("ส่ง c = ล้าง log");

  WiFi.mode(WIFI_STA);
}

void loop() {
  uint32_t now = millis();

  if (Serial.available() && Serial.read() == 'c') {
    prefs.clear();
    brownouts = 0;
    Serial.println("ล้าง log แล้ว");
  }

  // ปกติกะพริบ 1 ครั้งต่อวินาที ถ้าเคย brownout กะพริบเร็ว 5 ครั้งต่อวินาที
  digitalWrite(LED, (now / (brownouts ? 100 : 500)) % 2);

  // เริ่มสแกน Wi-Fi แบบไม่รอ (async) วิทยุจะกินกระแสสูงช่วงสั้นๆ
  static uint32_t lastScan = 0;
  if (!scanning && now - lastScan >= SCAN_EVERY_MS) {
    WiFi.scanNetworks(true);
    scanning = true;
    lastScan = now;
  }

  // ระหว่างสแกน วัดแบตไปด้วย เก็บค่าต่ำสุด (ต่ำกว่า 1 V = ไม่ได้ต่อตัวแบ่ง ไม่นับ)
  if (scanning) {
    uint32_t v = readVbatMv();
    if (v > 1000 && v < minVbatMv) minVbatMv = v;
    int n = WiFi.scanComplete();
    if (n >= 0 || n == WIFI_SCAN_FAILED) {
      Serial.printf("[%lu s] scan = %d AP  VBAT = %lu mV  ต่ำสุดรอบนี้ = %lu mV\n",
                    (unsigned long)(now / 1000), n, (unsigned long)v,
                    (unsigned long)(minVbatMv == UINT32_MAX ? 0 : minVbatMv));
      WiFi.scanDelete();
      scanning = false;
    }
  }

  // จดเวลาที่รันได้และ VBAT ต่ำสุดลง flash เป็นระยะ (ถ้าไฟตกจะรู้ว่ารันได้นานแค่ไหน)
  static uint32_t lastSave = 0;
  if (now - lastSave >= SAVE_EVERY_MS) {
    lastSave = now;
    prefs.putUInt("upS", now / 1000);
    prefs.putUInt("minV", minVbatMv == UINT32_MAX ? 0 : minVbatMv);
  }
}
