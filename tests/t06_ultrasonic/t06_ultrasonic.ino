// tests/t06_ultrasonic/t06_ultrasonic.ino
const int TRIG = 25;
const int ECHO = 26;

float readCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);   // timeout 30 ms ≈ 5 m
  if (us == 0) return -1;                          // ไม่มีเสียงสะท้อน
  return us * 0.0343f / 2.0f;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
}

void loop() {
  float d = readCm();
  if (d < 0) Serial.println("no echo (timeout)");
  else       Serial.printf("dist = %.1f cm\n", d);
  delay(200);
}
