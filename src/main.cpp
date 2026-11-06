#include "header.h"
#include "WebDashboard.h"

void setup() {
    Serial.begin(115200);

    // Prevent USB serial output from waiting when the monitor is disconnected.
    Serial.setTxTimeoutMs(0);

    Wire.begin(2, 1);

    oled_initialization();
    mpu_initialization();
    initialize_touch_reset();
    initialize_web_dashboard();
    heart_initialization();
}

void loop() {
    read_heart_sensor();
    handle_web_dashboard();
    read_heart_sensor();

    static unsigned long lastMotionReadMs = millis();
    static unsigned long lastPrintMs = millis();
    static unsigned long lastDisplayMs = millis();

    static xyzFloat upDirection{};
    static OledWarnings setWarnings{};
    static float curlAngleDegrees = 60.0f;
    static int repCount = 0;

    constexpr float GRAVITY_MPS2 = 9.81f;

    // Process motion approximately every 10 ms without blocking other tasks.
    const unsigned long currentTimeMs = millis();
    const unsigned long elapsedReadMs = currentTimeMs - lastMotionReadMs;

    if (elapsedReadMs < 10) return;

    lastMotionReadMs = currentTimeMs;

    const xyzFloat accelerationG = motionSensor.getGValues();
    const xyzFloat angularVelocityDps = motionSensor.getGyrValues();
    const float elapsedSeconds = elapsedReadMs / 1000.0f;

    // Establish the initial up direction before tracking movement.
    if (!initialize_up_direction(upDirection, accelerationG)) return;

    // Remove gravity from sideways acceleration and integrate angular velocity.
    update_orientation(upDirection, accelerationG, angularVelocityDps, elapsedSeconds);
    const float sideAccelerationMps2 = (accelerationG.y - upDirection.y) * GRAVITY_MPS2;
    curlAngleDegrees += angularVelocityDps.y * elapsedSeconds;

    if (is_touch_reset_pressed()) isSetDone = true;

    // Preserve the reset event before update_set_warnings clears the flag.
    const bool isSetReset = isSetDone;
    update_rep_count(curlAngleDegrees, repCount, isSetDone);

    if (!isSetDone) check_curl_speed(angularVelocityDps.y);

    update_set_warnings(setWarnings, angularVelocityDps.y, sideAccelerationMps2, isSetDone);
    update_neopixel(angularVelocityDps.y, sideAccelerationMps2);

    const HeartReadings heartReadings = get_heart_readings();
    update_web_readings(curlAngleDegrees, repCount, sideAccelerationMps2, setWarnings, heartReadings, isSetReset);

    // Limit serial output to 5 Hz and OLED updates to 10 Hz.
    if (currentTimeMs - lastPrintMs >= 200) {
        lastPrintMs = currentTimeMs;
        print_readings(curlAngleDegrees, repCount, sideAccelerationMps2, heartReadings);
    }

    if (currentTimeMs - lastDisplayMs >= 100) {
        lastDisplayMs = currentTimeMs;
        update_oled(curlAngleDegrees, repCount, setWarnings, heartReadings);
    }
}