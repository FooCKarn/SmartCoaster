// tests/t03_pir/t03_pir.ino
const int PIR_PIN = 27;
volatile bool motionFlag = false;
volatile uint32_t motionCount = 0;

void IRAM_ATTR onMotion() {          // ISR: สั้นที่สุด ห้าม Serial.print ในนี้
  motionFlag = true;
  motionCount++;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotion, RISING);
  Serial.println("PIR warming up 60 s... อย่าขยับหน้าเซนเซอร์");
  delay(60000);
  Serial.println("PIR ready");
}

void loop() {
  if (motionFlag) {                  // deferred processing ใน loop
    motionFlag = false;
    Serial.printf("[%lu ms] MOTION #%lu\n", millis(), motionCount);
  }
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("OUT=%d\n", digitalRead(PIR_PIN));
  }
}
