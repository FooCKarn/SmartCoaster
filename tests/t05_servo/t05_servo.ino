// tests/t05_servo/t05_servo.ino  (ESP32 Arduino core 3.x)
const int SERVO_PIN = 18;
const int STOP_BTN  = 33;
volatile bool estop = false;

void IRAM_ATTR onStop() { estop = true; }

void servoWriteUs(int us) {                      // 500–2400 µs
  uint32_t duty = (uint32_t)us * 65535UL / 20000UL;   // คาบ 20 ms, 16 บิต
  ledcWrite(SERVO_PIN, duty);
}
void servoWriteDeg(int deg) {
  deg = constrain(deg, 0, 180);
  servoWriteUs(map(deg, 0, 180, 500, 2400));
}

void setup() {
  Serial.begin(115200);
  pinMode(STOP_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(STOP_BTN), onStop, FALLING);
  ledcAttach(SERVO_PIN, 50, 16);                 // 50 Hz, 16-bit
  servoWriteDeg(0);
  Serial.println("Servo test: 0 -> 90 -> 0 ทุก 2 วินาที, กดปุ่มเพื่อหยุด");
}

void loop() {
  if (estop) {
    ledcWrite(SERVO_PIN, 0);                      // ตัดพัลส์ เซอร์โวหยุดออกแรง
    Serial.println("E-STOP pressed. Servo released. กด EN เพื่อเริ่มใหม่");
    while (true) delay(1000);
  }
  Serial.println("-> 90 deg (ยกป้าย)");  servoWriteDeg(90); delay(2000);
  Serial.println("-> 0 deg (ลดป้าย)");   servoWriteDeg(0);  delay(2000);
}
