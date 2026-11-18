// tests/t08_hx711_weigh/t08_hx711_weigh.ino
#include <HX711.h>
#include <LittleFS.h>
const int HX_DT = 19, HX_SCK = 18;
const char* CAL_FILE = "/cal.txt";
float CAL_FACTOR = 426.76f;           // ค่า default ถ้าไม่มีไฟล์ /cal.txt
HX711 scale;

void loadCal() {
  File f = LittleFS.open(CAL_FILE, "r");
  if (!f) { Serial.println("ไม่พบ /cal.txt - ใช้ค่า default"); return; }
  float v = f.readStringUntil('\n').toFloat();
  f.close();
  if (v != 0) CAL_FACTOR = v;
  Serial.printf("โหลด CAL_FACTOR = %.2f\n", CAL_FACTOR);
}

void setup() {
  Serial.begin(115200);
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount ไม่สำเร็จ");
  loadCal();
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
