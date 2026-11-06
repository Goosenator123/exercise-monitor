#include <Arduino.h>
#include <Wire.h>

// Variables
constexpr uint8_t sensorAddress{0b1101000};
constexpr uint8_t whoAmIRegister{0x75};

// put function declarations here:


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Wire.begin(2, 1);
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(1000);
  Wire.beginTransmission(sensorAddress);
  Wire.write(whoAmIRegister);
  uint8_t error = Wire.endTransmission(false);
  
  if (error != 0) {
    Serial.print("I2C write error: ");
    Serial.println(error);
    return;
  }

  if (Wire.requestFrom(sensorAddress, static_cast<uint8_t>(1)) != 1) {
    Serial.println("No byte received from the sensor.");
    return;
  }

  int modelId = Wire.read();
  Serial.print("WHO_AM_I = 0x");
  Serial.println(modelId, HEX);
}

// put function definitions here: