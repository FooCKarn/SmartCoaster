// tests/t02_dht11/t02_dht11.ino
#include <DHT.h>
#define DHT_PIN 14
DHT dht(DHT_PIN, DHT11);

void setup() {
  Serial.begin(115200);
  dht.begin();
  Serial.println("DHT11 test");
}

void loop() {
  delay(2000);                       // DHT11 อ่านได้เร็วสุด ~1 ครั้ง/วินาที
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("DHT read FAILED");
    return;
  }
  Serial.printf("T = %.1f C  RH = %.0f %%\n", t, h);
}
