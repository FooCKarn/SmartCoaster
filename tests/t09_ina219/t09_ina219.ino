// tests/t09_ina219/t09_ina219.ino
#include <Wire.h>
#include <Adafruit_INA219.h>

const int SDA_PIN = 32, SCL_PIN = 35;   // I2C ของ ESP32 (ขา default)
Adafruit_INA219 ina;                    // ที่อยู่ 0x40 (A0/A1 ไม่ต่อ)

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  while (!ina.begin()) { Serial.println("INA219 not found - เช็คสาย SDA/SCL/VCC"); delay(500); }
  ina.setCalibration_16V_400mA();       // ช่วง 16 V / 400 mA ละเอียดกว่า เหมาะกับโหนดที่กินไฟน้อย
  Serial.println("INA219 test (0x40)");
}

void loop() {
  float busV    = ina.getBusVoltage_V();     // แรงดันฝั่ง VIN− (ฝั่งโหลด)
  float shuntMv = ina.getShuntVoltage_mV();  // แรงดันตกคร่อม shunt 0.1 Ω
  float mA      = ina.getCurrent_mA();
  float mW      = ina.getPower_mW();
  Serial.printf("bus = %.3f V  shunt = %.2f mV  I = %.1f mA  P = %.1f mW\n", busV, shuntMv, mA, mW);
  delay(1000);
}
