#include <Arduino.h>

const int TRIG_CANDIDATES[] = {32, 33};
const int ECHO_CANDIDATES[] = {25, 26};

int trigPin = -1;
int echoPin = -1;

long readDuration(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  return pulseIn(echo, HIGH, 30000);
}

void releasePins() {
  for (int p : TRIG_CANDIDATES) pinMode(p, INPUT);
  for (int p : ECHO_CANDIDATES) pinMode(p, INPUT_PULLDOWN);
}

bool findPins() {
  Serial.println("Scanning pins... (put your hand 10-50 cm in front of the sensor)");
  for (int t : TRIG_CANDIDATES) {
    for (int e : ECHO_CANDIDATES) {
      releasePins();
      pinMode(t, OUTPUT);
      digitalWrite(t, LOW);
      delay(50);

      if (digitalRead(e) == HIGH) {
        Serial.printf("  TRIG=GPIO%d ECHO=GPIO%d -> ECHO stuck HIGH\n", t, e);
        continue;
      }

      int hits = 0;
      for (int i = 0; i < 3; i++) {
        if (readDuration(t, e) > 0) hits++;
        delay(60);
      }
      Serial.printf("  TRIG=GPIO%d ECHO=GPIO%d -> %d/3 echoes\n", t, e, hits);
      if (hits >= 2) {
        trigPin = t;
        echoPin = e;
        Serial.printf("FOUND: TRIG=GPIO%d  ECHO=GPIO%d\n\n", t, e);
        return true;
      }
    }
  }
  releasePins();
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== HC-SR04 test ===");
}

void loop() {
  if (trigPin < 0) {
    if (!findPins()) {
      Serial.println("No echo on any pin pair. Check VCC/GND and the TRIG/ECHO wires. Retrying...\n");
      delay(2000);
    }
    return;
  }

  long us = readDuration(trigPin, echoPin);
  if (us == 0) {
    Serial.println("no echo (nothing within ~4 m, or wiring problem)");
  } else {
    float cm = us * 0.0343 / 2.0;
    Serial.printf("%6ld us  ->  %6.1f cm\n", us, cm);
  }
  delay(200);
}
