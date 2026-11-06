#include "header.h"

// Prevent the heart library's buffer macro from replacing Wire's definition.
#pragma push_macro("I2C_BUFFER_LENGTH")
#undef I2C_BUFFER_LENGTH
#include <MAX30105.h>
#pragma pop_macro("I2C_BUFFER_LENGTH")

#include <heartRate.h>

namespace {

MAX30105 heartSensor;
HeartReadings heartReadings;

constexpr uint32_t HEART_SAMPLE_RATE_HZ = 100;
constexpr uint32_t FINGER_IR_THRESHOLD = 50000;

uint32_t heartSampleCount = 0;
uint32_t lastBeatSampleNumber = 0;
uint32_t lastHeartPollMs = 0;
uint32_t lastHeartDataMs = 0;
bool hasPreviousBeat = false;

uint32_t beatIntervalsSamples[4]{};
uint8_t beatIntervalCount = 0;
uint8_t beatIntervalIndex = 0;

// Clear the BPM history without resetting the sensor or library's beat filter.
void reset_heart_estimate() {
    heartReadings.isBpmAvailable = false;
    heartReadings.heartRateBpm = 0.0f;
    hasPreviousBeat = false;
    beatIntervalCount = 0;
    beatIntervalIndex = 0;
}

void process_heart_sample(uint32_t redLightReading, uint32_t infraredReading) {
    heartReadings.redLightReading = redLightReading;
    heartReadings.infraredReading = infraredReading;
    ++heartSampleCount;

    // Keep the beat filter running with scaled IR values, even without contact.
    const bool isBeatDetected = checkForBeat(static_cast<int32_t>(infraredReading / 4));

    heartReadings.isFingerPresent = infraredReading >= FINGER_IR_THRESHOLD;

    if (!heartReadings.isFingerPresent) {
        reset_heart_estimate();
        return;
    }

    if (
        hasPreviousBeat &&
        heartSampleCount - lastBeatSampleNumber > 3 * HEART_SAMPLE_RATE_HZ
    ) reset_heart_estimate();

    if (!isBeatDetected) return;

    // The first beat sets the reference; the second provides an interval.
    if (!hasPreviousBeat) {
        hasPreviousBeat = true;
        lastBeatSampleNumber = heartSampleCount;
        return;
    }

    const uint32_t beatIntervalSamples = heartSampleCount - lastBeatSampleNumber;

    lastBeatSampleNumber = heartSampleCount;

    // Accept intervals of 0.24 to just under 3 seconds at 100 samples/second.
    if (beatIntervalSamples < 24 || beatIntervalSamples >= 300) return;

    // Circular buffer: retain up to four recent accepted intervals.
    beatIntervalsSamples[beatIntervalIndex] = beatIntervalSamples;
    beatIntervalIndex = (beatIntervalIndex + 1) % 4;

    if (beatIntervalCount < 4) ++beatIntervalCount;

    uint32_t totalIntervalSamples = 0;

    for (uint8_t i = 0; i < beatIntervalCount; ++i) {
        totalIntervalSamples += beatIntervalsSamples[i];
    }

    // Convert the average beat interval from sample counts into BPM.
    heartReadings.heartRateBpm = 60.0f * HEART_SAMPLE_RATE_HZ * beatIntervalCount / totalIntervalSamples;

    heartReadings.isBpmAvailable = beatIntervalCount >= 1;
}

} // namespace

void heart_initialization() {
    heartReadings.isConnected = heartSensor.begin(Wire, I2C_SPEED_FAST);

    if (!heartReadings.isConnected) {
        Serial.println("MAX30102 not found. Check wiring and power.");
        return;
    }

    // Brightness, averaging, LED mode, sample rate, pulse width, ADC range.
    // Averaging four samples at 400 Hz produces 100 output samples/second.
    heartSensor.setup(0x1F, 4, 2, 400, 411, 4096);
    heartSensor.setPulseAmplitudeRed(0x0A);
    heartSensor.clearFIFO();

    lastHeartPollMs = millis();

    Serial.println("MAX30102 ready. Hold a finger gently and steadily on it.");
}

void read_heart_sensor(bool isForcedRead) {
    if (!heartReadings.isConnected) return;

    const uint32_t currentTimeMs = millis();

    if (!isForcedRead && currentTimeMs - lastHeartPollMs < 5) return;

    const bool hasLongReadGap = currentTimeMs - lastHeartPollMs > 200;
    lastHeartPollMs = currentTimeMs;

    const uint16_t fetchedSampleCount = heartSensor.check();

    // Discard potentially incomplete batches after a polling gap or when
    // the library's four-entry buffer limit is reached.
    if (hasLongReadGap || fetchedSampleCount >= 4) {
        ++heartReadings.droppedBatchCount;

        while (heartSensor.available()) heartSensor.nextSample();

        heartSensor.clearFIFO();
        reset_heart_estimate();
        return;
    }

    if (fetchedSampleCount > 0) lastHeartDataMs = currentTimeMs;

    if (currentTimeMs - lastHeartDataMs > 1000) reset_heart_estimate();

    while (heartSensor.available()) {
        process_heart_sample(
            heartSensor.getFIFORed(),
            heartSensor.getFIFOIR()
        );

        heartSensor.nextSample();
    }
}

const HeartReadings& get_heart_readings() {
    return heartReadings;
}