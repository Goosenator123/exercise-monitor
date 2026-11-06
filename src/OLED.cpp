#include "header.h"

// Keep the shared I2C bus at 400 kHz during and after OLED transfers.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 400000, 400000);

void oled_initialization() {
    // Use the I2C bus already initialized in setup().
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true) {}
    }

    display.display();
    delay(2000);
    display.clearDisplay();
}

void update_oled(float curlAngleDegrees, int repCount, const OledWarnings& setWarnings, const HeartReadings& heartReadings) {
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);

    // Map 0–120 degrees to a vertical bar that fills from bottom to top.
    constexpr int barWidthPixels = 12;
    constexpr int barInsetPixels = 2;
    const int barInnerHeightPixels = display.height() - 2 * barInsetPixels;
    const float progressFraction = constrain(curlAngleDegrees / 120.0f, 0.0f, 1.0f);
    const int barFillHeightPixels = static_cast<int>(lroundf(progressFraction * barInnerHeightPixels));

    display.drawRect(0, 0, barWidthPixels, display.height(), SSD1306_WHITE);

    if (barFillHeightPixels > 0) {
        display.fillRect(barInsetPixels, display.height() - barInsetPixels - barFillHeightPixels, barWidthPixels - 2 * barInsetPixels, barFillHeightPixels, SSD1306_WHITE);
    }

    display.setTextSize(1);
    display.setCursor(20, 4);
    display.print("Angle:");
    display.setCursor(20, 28);
    display.print("Reps:");

    display.setTextSize(2);
    display.setCursor(68, 0);
    display.printf("%d", static_cast<int>(lroundf(curlAngleDegrees)));
    display.setCursor(68, 24);

    if (repCount > 99999) {
        display.setTextSize(1);
    }

    display.printf("%d", repCount);

    display.setTextSize(1);
    display.setCursor(20, 16);

    if (heartReadings.isBpmAvailable) {
        display.printf("BPM: %.0f", heartReadings.heartRateBpm);
    } else {
        display.print("BPM: --");
    }

    display.setCursor(20, 46);

    if (setWarnings.hasSpeedWarning) display.print("TOO FAST");

    display.setCursor(20, 56);

    if (setWarnings.hasSwayWarning) display.print("EXCESSIVE SWAY");

    // Process heart samples before and after the OLED transfer to reduce buffer loss.
    read_heart_sensor(true);
    display.display();
    read_heart_sensor(true);
}