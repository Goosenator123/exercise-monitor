#pragma once

#include <Arduino.h>
#include <MPU6500_WE.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 display;
extern MPU6500_WE motionSensor;

constexpr uint8_t OLED_ADDR{0x3C};
constexpr uint8_t MPU_6500_ADDR{0b1101000};
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;

constexpr uint8_t NEOPIXEL_PIN = 48;
constexpr uint8_t RESET_TOUCH_PIN = 5;

struct HeartReadings {
    bool isConnected = false;
    bool isFingerPresent = false;
    bool isBpmAvailable = false;
    uint32_t redLightReading = 0;
    uint32_t infraredReading = 0;
    float heartRateBpm = 0.0f;

    // Count sample batches discarded by the heart-sensor processing logic.
    uint32_t droppedBatchCount = 0;
};

// Shared flag requesting that the current exercise set be reset.
extern bool isSetDone;

struct OledWarnings {
    bool hasSpeedWarning = false;
    bool hasSwayWarning = false;
};

// Initialize the OLED, motion sensor, and indicator LED.
void oled_initialization();
void mpu_initialization();
void neopixel_initialization();

// Estimate the initial up direction from stationary accelerometer samples.
// Return true once initialization is complete.
bool initialize_up_direction(
    xyzFloat& upDirection,
    const xyzFloat& accelerationG
);

// Update the estimated up direction using acceleration, angular velocity,
// and the time elapsed since the previous measurement.
void update_orientation(
    xyzFloat& upDirection,
    const xyzFloat& accelerationG,
    const xyzFloat& angularVelocityDps,
    float elapsedSeconds
);

// Check curl angular speed and report excessive speed through Serial.
void check_curl_speed(float curlAngularVelocityDps);

// Track curl stages and count completed repetitions.
// Optionally reset the repetition-tracking state.
void update_rep_count(
    float& curlAngleDegrees,
    int& repCount,
    bool isResetRequested = false
);

// Print motion and heart-rate readings to the Serial Monitor.
void print_readings(
    float curlAngleDegrees,
    int repCount,
    float sideAccelerationMps2,
    const HeartReadings& heartReadings
);

// Latch speed and sway warnings for the set, or clear them on reset.
// The reset flag is passed by reference so the function can clear it.
void update_set_warnings(
    OledWarnings& setWarnings,
    float curlAngularVelocityDps,
    float sideAccelerationMps2,
    bool& isResetRequested
);

// Update the indicator LED based on curl speed and sideways acceleration.
void update_neopixel(
    float curlAngularVelocityDps,
    float sideAccelerationMps2
);

// Display the curl angle, repetition count, warnings, and heart-rate status.
void update_oled(
    float curlAngleDegrees,
    int repCount,
    const OledWarnings& setWarnings,
    const HeartReadings& heartReadings
);

// Initialize and configure the heart-rate sensor.
void heart_initialization();

// Process available heart-sensor samples.
// A forced read bypasses the normal polling interval.
void read_heart_sensor(bool isForcedRead = false);

// Provide read-only access to the latest heart-sensor results without copying.
const HeartReadings& get_heart_readings();

// Establish the capacitive-touch baseline for detecting a reset press.
void initialize_touch_reset();

// Check for a new debounced touch press.
bool is_touch_reset_pressed();