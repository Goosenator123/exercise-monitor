#include <Arduino.h>
#include <SPI.h>

constexpr uint8_t clockPin{1};  // MPU SCL/SCLK
constexpr uint8_t mosiPin{2};   // MPU SDA/SDI
constexpr uint8_t misoPin{3};  // MPU AD0/SDO
constexpr uint8_t csPin{4};     // MPU NCS/CS

constexpr uint8_t whoAmIRegister = 0x75;
constexpr uint8_t readBit = 0x80;  // Binary: 1000 0000 for reading

SPISettings sensorSetting1(1000000, MSBFIRST, SPI_MODE0);

void setup() {
    Serial.begin(115200);
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    SPI.begin(clockPin, misoPin, mosiPin);
}

void loop() {
    SPI.beginTransaction(sensorSetting1);
    digitalWrite(csPin, LOW);
    SPI.transfer(whoAmIRegister | readBit);
    uint8_t modelId = SPI.transfer(0x00);
    digitalWrite(csPin, HIGH);
    SPI.endTransaction();

    Serial.print("WHO_AM_I = 0x");
    Serial.println(modelId, HEX);
    Serial.println("This is done through SPI protocol");
    delay(1000);
}