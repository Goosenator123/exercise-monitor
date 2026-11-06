#include "header.h"

void print_readings(float curlAngleDegrees, int repCount, float sideAccelerationMps2, const HeartReadings& heartReadings) {
    if (!Serial) return;

    Serial.printf("Time: %lu | Angle: %.1f | Reps: %i | Red: %lu | IR: %lu | ", millis(), curlAngleDegrees, repCount, static_cast<unsigned long>(heartReadings.redLightReading), static_cast<unsigned long>(heartReadings.infraredReading));

    Serial.printf("HR dropped batches: %lu | ", static_cast<unsigned long>(heartReadings.droppedBatchCount));

    if (!heartReadings.isConnected) {
        Serial.println("HR sensor unavailable");
    } else if (!heartReadings.isFingerPresent) {
        Serial.println("HR: place finger");
    } else if (!heartReadings.isBpmAvailable) {
        Serial.println("HR: detecting...");
    } else {
        Serial.printf("HR: %.0f BPM\n", heartReadings.heartRateBpm);
    }

    if (fabsf(sideAccelerationMps2) > 3.0f) {
        Serial.println("EXCESSIVE SIDEWAYS MOVEMENT");
    }
}

bool isSetDone = false;

// Keep warnings active until a reset clears them and consumes the reset request.
void update_set_warnings(OledWarnings& setWarnings, float curlAngularVelocityDps, float sideAccelerationMps2, bool& isResetRequested) {
    if (isResetRequested) {
        setWarnings = {};
        isResetRequested = false;
        return;
    }

    if (fabsf(curlAngularVelocityDps) > 120.0f) setWarnings.hasSpeedWarning = true;

    if (fabsf(sideAccelerationMps2) > 3.0f) setWarnings.hasSwayWarning = true;
}

namespace {
float touchBaseline = 0.0f;
}

// Average 20 startup readings with the touch pad untouched.
void initialize_touch_reset() {
    touchRead(RESET_TOUCH_PIN);
    delay(20);

    uint64_t total = 0;

    for (int sample = 0; sample < 20; ++sample) {
        total += touchRead(RESET_TOUCH_PIN);
        delay(5);
    }

    touchBaseline = total / 20.0f;
}

// Return true only for a newly confirmed press, not continuously while held.
bool is_touch_reset_pressed() {
    static bool isTouchCandidate = false;
    static bool isTouchConfirmed = false;
    static unsigned long lastChangeMs = 0;

    if (touchBaseline <= 0.0f) return false;

    const uint32_t touchValue = touchRead(RESET_TOUCH_PIN);
    const unsigned long currentTimeMs = millis();

    // Separate press and release thresholds prevent toggling near a single threshold.
    const float threshold = touchBaseline * (isTouchCandidate ? 1.2f : 1.5f);
    const bool isTouched = touchValue > threshold;

    if (isTouched != isTouchCandidate) {
        isTouchCandidate = isTouched;
        lastChangeMs = currentTimeMs;
    }

    // Accept a state change only after it remains unchanged for 50 ms.
    if (currentTimeMs - lastChangeMs >= 50 && isTouchConfirmed != isTouchCandidate) {
        isTouchConfirmed = isTouchCandidate;
        return isTouchConfirmed;
    }

    return false;
}