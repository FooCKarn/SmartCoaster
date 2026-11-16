// tests/t04_sound/t04_sound.ino
const int SND_AO = 35;
const int SND_DO = 32;

int soundP2P(uint32_t windowMs = 50) {
  int lo = 4095, hi = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < windowMs) {
    int v = analogRead(SND_AO);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return hi - lo;
}

void setup() {
  Serial.begin(115200);
  pinMode(SND_DO, INPUT);
  analogReadResolution(12);          // 0–4095
}

void loop() {
  int p2p = soundP2P();
  Serial.printf("p2p=%4d  DO=%d  ", p2p, digitalRead(SND_DO));
  for (int i = 0; i < p2p / 20; i++) Serial.print('#');   // แถบระดับเสียง
  Serial.println();
  delay(100);
}
