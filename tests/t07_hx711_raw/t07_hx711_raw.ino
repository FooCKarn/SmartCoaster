// tests/t07_hx711_raw/t07_hx711_raw.ino
#include <HX711.h>
const int HX_DT = 19, HX_SCK = 18;
HX711 scale;

void setup() {
  Serial.begin(115200);
  scale.begin(HX_DT, HX_SCK);   // ตั้งขา DT เป็น input และ SCK เป็น output
}

void loop() {
  if (scale.is_ready()) {
    long raw = scale.read();     // ค่าดิบ 1 ครั้ง ยังไม่หักค่าศูนย์
    Serial.printf("DT=%d  raw=%ld\n", digitalRead(HX_DT), raw);
  } else {
    Serial.println("not ready");
  }
  delay(500);
}
