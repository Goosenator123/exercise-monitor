#include "header.h"

void neopixel_initialization() {
    rgbLedWriteOrdered(NEOPIXEL_PIN, LED_COLOR_ORDER_GRB, 0, 0, 0);
}

void update_neopixel(float curlAngularVelocityDps, float sideAccelerationMps2) {
    constexpr float FAST_SPEED_THRESHOLD_DPS = 120.0f;
    constexpr float SWAY_THRESHOLD_MPS2 = 3.0f;
    constexpr unsigned long FLASH_DURATION_MS = 1000;
    constexpr unsigned long BLINK_INTERVAL_MS = 100;

    static bool isFlashing = false;
    static bool isLedOn = false;
    static unsigned long flashStartMs = 0;

    const unsigned long currentTimeMs = millis();

    // Restart the timer while either threshold is exceeded, keeping the LED on.
    if (fabsf(curlAngularVelocityDps) > FAST_SPEED_THRESHOLD_DPS || fabsf(sideAccelerationMps2) > SWAY_THRESHOLD_MPS2) {
        isFlashing = true;
        flashStartMs = currentTimeMs;
    }

    bool shouldLightOn = false;

    // Once the thresholds clear, alternate on/off every 100 ms until the timer expires.
    if (isFlashing) {
        const unsigned long elapsedFlashMs = currentTimeMs - flashStartMs;

        if (elapsedFlashMs >= FLASH_DURATION_MS) {
            isFlashing = false;
        } else {
            shouldLightOn = ((elapsedFlashMs / BLINK_INTERVAL_MS) % 2) == 0;
        }
    }

    // Write to the LED only when its on/off state changes.
    if (shouldLightOn != isLedOn) {
        isLedOn = shouldLightOn;

        if (isLedOn) {
            rgbLedWriteOrdered(NEOPIXEL_PIN, LED_COLOR_ORDER_GRB, 0, RGB_BRIGHTNESS, 0);
        } else {
            rgbLedWriteOrdered(NEOPIXEL_PIN, LED_COLOR_ORDER_GRB, 0, 0, 0);
        }
    }
}