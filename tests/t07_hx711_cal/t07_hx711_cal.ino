// tests/t07_hx711_cal/t07_hx711_cal.ino
#include <HX711.h>
#include <LittleFS.h>
const int HX_DT = 19, HX_SCK = 18;
const char* CAL_FILE = "/cal.txt";
HX711 scale;

float factorSum = 0;   // รวมค่า factor ทุกรอบ
int factorCount = 0;   // จำนวนรอบที่ calibrate

void saveCal(float factor) {
  File f = LittleFS.open(CAL_FILE, "w");   // เขียนทับไฟล์เดิม
  if (!f) { Serial.println("เปิดไฟล์ไม่ได้ - เซฟไม่สำเร็จ"); return; }
  f.println(factor, 2);
  f.close();
  Serial.printf("เซฟลง %s แล้ว: %.2f\n", CAL_FILE, factor);
}

void setup() {
  Serial.begin(115200);
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount ไม่สำเร็จ");
  scale.begin(HX_DT, HX_SCK);

  while (!scale.is_ready()) { Serial.println("HX711 not ready - เช็คสาย"); delay(500); }
  Serial.println("ยกทุกอย่างออกจากแท่น แล้วรอ 3 วินาที...");
  delay(3000);
  scale.set_scale();          // scale = 1 (ค่าดิบ)
  scale.tare(20);             // ตั้งศูนย์จากค่าเฉลี่ย 20 ครั้ง
  Serial.println("วางของที่รู้น้ำหนัก แล้วพิมพ์น้ำหนักเป็นกรัม (เช่น 500) แล้วกด Enter");
}

void loop() {
  if (Serial.available()) {
    float known = Serial.parseFloat();
    if (known > 0) {
      float raw = scale.get_value(20);        // ค่าเฉลี่ย 20 ครั้ง หลังหัก tare
      float factor = raw / known;
      Serial.printf("raw = %.0f  known = %.1f g  ->  CAL_FACTOR = %.2f\n", raw, known, factor);

      factorSum += factor;
      factorCount++;
      float avg = factorSum / factorCount;    // เฉลี่ยทุกรอบที่ทำใน session นี้
      Serial.printf("รอบที่ %d  ค่าเฉลี่ย CAL_FACTOR = %.2f\n", factorCount, avg);
      saveCal(avg);
      Serial.println("ทำซ้ำอีก 2 รอบด้วยน้ำหนักต่างกัน (ไฟล์จะเก็บค่าเฉลี่ยล่าสุดให้เอง)");
    }
  }
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("raw(avg5) = %.0f\n", scale.get_value(5));
  }
}
