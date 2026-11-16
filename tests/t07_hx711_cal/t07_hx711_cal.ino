// tests/t07_hx711_cal/t07_hx711_cal.ino
#include <HX711.h>
const int HX_DT = 19, HX_SCK = 18;
HX711 scale;

void setup() {
  Serial.begin(115200);
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
      Serial.println("จดค่านี้ไว้ แล้วทำซ้ำอีก 2 รอบด้วยน้ำหนักต่างกัน");
    }
  }
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("raw(avg5) = %.0f\n", scale.get_value(5));
  }
}
