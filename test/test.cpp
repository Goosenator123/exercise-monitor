#include <Arduino.h>

void setup() {
    Serial.begin(115200);
}

void loop() {
    Serial.println("ESP32 is running");
    delay(1000);
}