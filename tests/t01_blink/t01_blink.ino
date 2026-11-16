// tests/t01_blink/t01_blink.ino
#include <WiFi.h>
#include <esp_wifi.h>

const int LED = 2;   // LED บนบอร์ด (GPIO2)

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  WiFi.mode(WIFI_STA);              // ต้องเปิด Wi-Fi ก่อนถึงจะอ่าน MAC ได้
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  Serial.printf("\nBoard OK. STA MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void loop() {
  digitalWrite(LED, HIGH); delay(500);
  digitalWrite(LED, LOW);  delay(500);
  Serial.println("blink");
}
