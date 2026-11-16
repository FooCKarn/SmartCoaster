// tests/t08_hx711_weigh/t08_hx711_weigh.ino
#include <HX711.h>
const int HX_DT = 19, HX_SCK = 18;
const float CAL_FACTOR = 426.76f;     // ← ใส่ค่าของตัวเองจากขั้น A
HX711 scale;

void setup() {
  Serial.begin(115200);
  scale.begin(HX_DT, HX_SCK);
  scale.set_scale(CAL_FACTOR);
  delay(2000);
  scale.tare(20);
  Serial.println("Ready. ส่ง 't' เพื่อ tare ใหม่");
}

void loop() {
  if (Serial.available() && Serial.read() == 't') { scale.tare(20); Serial.println("tared"); }
  float g = scale.get_units(10);
  Serial.printf("weight = %7.1f g\n", g);
  delay(500);
}
