// tests/t15_power_check/t15_power_check.ino
// หาว่าไฟจากแบตตกตอนไหน: เพิ่มโหลดทีละขั้น วัดแรงดันขั้นละ 5 วิ แล้วจดลง flash ทุกครึ่งวินาที
// ถ้าบอร์ดรีเซ็ตเพราะไฟตกกลางทาง ให้เสียบ USB แล้วเปิด Serial Monitor (115200)
// จะเห็นว่าตายที่ขั้นไหน และก่อนตายแรงดันต่ำสุดเท่าไร
//
// วัดแรงดันจากตัวแบ่ง 2 ตัว (10k/10k หรือ 100k/100k) ที่ขา D34 ตามวงจร node เดิม
//   ต่อปลายบนของตัวแบ่งที่ "ขั้วบวกแบต"     → วัด VBAT (ค่าปกติ 3.6-4.2 V)
//   หรือย้ายไปที่ "ขาออก + ของ MT3608"   → วัดไฟ 5V (ค่าปกติ 4.8-5.1 V)
// ไม่ได้ต่อตัวแบ่งก็รันได้ จะรู้แค่ว่าตายขั้นไหน (แรงดันขึ้น 0)
//
// LED สีฟ้ากะพริบ = เลขขั้นปัจจุบัน (1-6 ครั้ง) ทุก 5 วิ, ผ่านครบทุกขั้น = ติดค้าง
// Serial: c = ล้าง log
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

const int LED = 2;
const int VSENSE_PIN = 34;
const float DIVIDER = 2.0f;
const uint8_t WIFI_CHANNEL = 4;
const uint32_t STEP_MS = 5000;
const int NSTEP = 6;
const char *STEP_NAME[NSTEP + 1] = {
  "-",
  "1 CPU 80 MHz เฉยๆ (ไม่มีวิทยุ)",
  "2 CPU 240 MHz (ไม่มีวิทยุ)",
  "3 เปิด WiFi กำลังส่งต่ำสุด 2 dBm",
  "4 ส่ง ESP-NOW รัว ที่ 8.5 dBm",
  "5 ส่ง ESP-NOW รัว ที่ 13 dBm",
  "6 ส่ง ESP-NOW รัว ที่ 20 dBm (เต็มกำลัง)",
};
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Preferences log_;
int step = 0;
uint32_t minMv[NSTEP + 1], maxMv[NSTEP + 1];

uint32_t readMv() {                       // อ่านเร็ว (ไม่เฉลี่ย) เพื่อจับไฟตกสั้นๆ
  return analogReadMilliVolts(VSENSE_PIN) * DIVIDER;
}

void blinkN(int n) {
  for (int i = 0; i < n; i++) { digitalWrite(LED, HIGH); delay(120); digitalWrite(LED, LOW); delay(180); }
}

void printPrevious(esp_reset_reason_t reason) {
  uint8_t reached = log_.getUChar("step", 0);
  bool done = log_.getBool("done", false);
  Serial.println();
  Serial.println("===== t15 ผลรอบก่อน =====");
  Serial.printf("รีเซ็ตครั้งนี้เพราะ: %s\n",
                reason == ESP_RST_BROWNOUT ? "BROWNOUT (ไฟตก)" :
                reason == ESP_RST_POWERON ? "POWERON (เพิ่งจ่ายไฟ)" :
                reason == ESP_RST_EXT ? "EXT (ปุ่ม EN/เปิด Serial)" : "อื่นๆ");
  if (reached == 0) { Serial.println("ยังไม่มีข้อมูล (รันครั้งแรก หรือเพิ่งล้าง)"); }
  for (int s = 1; s <= reached && s <= NSTEP; s++) {
    char k1[8], k2[8];
    snprintf(k1, sizeof k1, "mn%d", s); snprintf(k2, sizeof k2, "mx%d", s);
    Serial.printf("  ขั้น %-42s ต่ำสุด %4lu mV  สูงสุด %4lu mV\n", STEP_NAME[s],
                  (unsigned long)log_.getUInt(k1, 0), (unsigned long)log_.getUInt(k2, 0));
  }
  if (reached > 0) {
    if (done) Serial.println("=> ผ่านครบทุกขั้น ไฟพอ");
    else Serial.printf("=> ไปไม่รอดที่ขั้น %s\n", STEP_NAME[reached]);
  }
  Serial.printf("brownout สะสม = %lu ครั้ง\n", (unsigned long)log_.getUInt("brown", 0));
  Serial.println("==========================  (ส่ง c = ล้าง log)");
}

void saveStep() {
  char k1[8], k2[8];
  snprintf(k1, sizeof k1, "mn%d", step); snprintf(k2, sizeof k2, "mx%d", step);
  log_.putUInt(k1, minMv[step] == UINT32_MAX ? 0 : minMv[step]);
  log_.putUInt(k2, maxMv[step]);
}

void enterStep(int s) {
  step = s;
  minMv[s] = UINT32_MAX; maxMv[s] = 0;
  log_.putUChar("step", s);               // จดก่อนลงมือ ถ้าตายจะรู้ว่าขั้นนี้
  saveStep();
  Serial.printf("\n>> ขั้น %s\n", STEP_NAME[s]);
  Serial.flush();
  switch (s) {
    case 1: setCpuFrequencyMhz(80); break;
    case 2: setCpuFrequencyMhz(240); break;
    case 3:
      WiFi.mode(WIFI_STA);
      esp_wifi_set_max_tx_power(8);       // 2 dBm
      esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
      esp_now_init();
      { esp_now_peer_info_t p = {}; memcpy(p.peer_addr, BCAST, 6); p.channel = WIFI_CHANNEL; esp_now_add_peer(&p); }
      break;
    case 4: esp_wifi_set_max_tx_power(34); break;
    case 5: esp_wifi_set_max_tx_power(52); break;
    case 6: esp_wifi_set_max_tx_power(80); break;
  }
  blinkN(s);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  analogSetPinAttenuation(VSENSE_PIN, ADC_11db);
  delay(1500);                            // รอเปิด Serial Monitor ทัน
  esp_reset_reason_t reason = esp_reset_reason();
  log_.begin("t15", false);
  if (reason == ESP_RST_BROWNOUT) log_.putUInt("brown", log_.getUInt("brown", 0) + 1);
  printPrevious(reason);

  // ถ้ารอบนี้เสียบ USB อยู่ ไม่ต้องรันทดสอบซ้ำ (จะทับผลรอบแบต) ให้ส่ง r เพื่อเริ่มเอง
  Serial.println("ส่ง s ภายใน 10 วิ = ไม่ทดสอบ (เก็บผลไว้อ่าน)  r = เริ่มทันที  ไม่ส่งอะไร = เริ่มเองใน 10 วิ");
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'c') { log_.clear(); Serial.println("ล้าง log แล้ว"); }
      if (c == 'r') break;
      if (c == 's') { Serial.println("ไม่เริ่มทดสอบ"); while (true) delay(1000); }
    }
    digitalWrite(LED, (millis() / 100) % 2);   // กะพริบรัวระหว่างรอ
  }
  digitalWrite(LED, LOW);
  log_.putBool("done", false);
  enterStep(1);
}

void loop() {
  if (step > NSTEP) { delay(1000); return; }  // ผ่านครบแล้ว
  static uint32_t stepStart = millis(), lastSave = 0;
  uint32_t mv = readMv();
  if (mv < minMv[step]) minMv[step] = mv;
  if (mv > maxMv[step]) maxMv[step] = mv;

  if (step >= 4 && step <= NSTEP) {        // ส่งแพ็กเก็ตรัวๆ = จำลองโหลดตอนส่งจริง
    static uint8_t payload[200];
    esp_now_send(BCAST, payload, sizeof payload);
    delay(5);
  } else {
    delay(2);
  }

  if (millis() - lastSave >= 500) { lastSave = millis(); saveStep(); }

  if (step <= NSTEP && millis() - stepStart >= STEP_MS) {
    saveStep();
    Serial.printf("   ต่ำสุด %lu mV  สูงสุด %lu mV\n", (unsigned long)minMv[step], (unsigned long)maxMv[step]);
    if (step == NSTEP) {
      log_.putBool("done", true);
      Serial.println("\nผ่านครบทุกขั้น ไฟพอ");
      digitalWrite(LED, HIGH);
      step = NSTEP + 1;
      return;
    }
    enterStep(step + 1);
    stepStart = millis();
  }
}
