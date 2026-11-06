#include "header.h"

MPU6500_WE motionSensor = MPU6500_WE(MPU_6500_ADDR);

// Calibrate while flat and stationary, then configure filters and measurement ranges.
void mpu_initialization() {
    if (!motionSensor.init()) Serial.println("MPU6500 did not respond");

    Serial.println("Position your MPU6500 flat and don't move it - calibrating . . .");
    delay(1000);
    motionSensor.autoOffsets();
    Serial.println("Calibration Complete");

    motionSensor.enableGyrDLPF();
    motionSensor.setGyrDLPF(MPU6500_DLPF_6);
    motionSensor.setSampleRateDivider(5);

    motionSensor.setGyrRange(MPU6500_GYRO_RANGE_500);
    motionSensor.setAccRange(MPU6500_ACC_RANGE_2G);

    motionSensor.enableAccDLPF(true);
    motionSensor.setAccDLPF(MPU6500_DLPF_6);
    delay(200);
}

// Combine 50 stationary samples and normalize them to establish the initial up direction.
bool initialize_up_direction(xyzFloat& upDirection, const xyzFloat& accelerationG) {
    static int startupSampleCount = 0;

    if (startupSampleCount < 50) {
        upDirection.x += accelerationG.x;
        upDirection.y += accelerationG.y;
        upDirection.z += accelerationG.z;
        ++startupSampleCount;

        if (startupSampleCount == 50) {
            const float directionMagnitude = sqrtf(upDirection.x * upDirection.x + upDirection.y * upDirection.y + upDirection.z * upDirection.z);

            // Retry initialization if the readings cannot provide a usable direction.
            if (directionMagnitude < 0.001f) {
                upDirection = {};
                startupSampleCount = 0;
                return false;
            }

            upDirection.x /= directionMagnitude;
            upDirection.y /= directionMagnitude;
            upDirection.z /= directionMagnitude;
        }

        return false;
    }

    return true;
}

void update_orientation(xyzFloat& upDirection, const xyzFloat& accelerationG, const xyzFloat& angularVelocityDps, float elapsedSeconds) {
    constexpr float DEGREES_TO_RADIANS = PI / 180.0f;

    const float angularVelocityXRad = angularVelocityDps.x * DEGREES_TO_RADIANS;
    const float angularVelocityYRad = angularVelocityDps.y * DEGREES_TO_RADIANS;
    const float angularVelocityZRad = angularVelocityDps.z * DEGREES_TO_RADIANS;

    // Cross product: calculate how the up vector changes in the rotating sensor's axes.
    const float upChangeRateX = upDirection.y * angularVelocityZRad - upDirection.z * angularVelocityYRad;
    const float upChangeRateY = upDirection.z * angularVelocityXRad - upDirection.x * angularVelocityZRad;
    const float upChangeRateZ = upDirection.x * angularVelocityYRad - upDirection.y * angularVelocityXRad;

    upDirection.x += upChangeRateX * elapsedSeconds;
    upDirection.y += upChangeRateY * elapsedSeconds;
    upDirection.z += upChangeRateZ * elapsedSeconds;

    const float accelerationMagnitudeG = sqrtf(accelerationG.x * accelerationG.x + accelerationG.y * accelerationG.y + accelerationG.z * accelerationG.z);
    const float angularSpeedDps = sqrtf(angularVelocityDps.x * angularVelocityDps.x + angularVelocityDps.y * angularVelocityDps.y + angularVelocityDps.z * angularVelocityDps.z);

    // Correct gyro drift using acceleration only when readings suggest the sensor is nearly still.
    if (fabsf(accelerationMagnitudeG - 1.0f) < 0.05f && angularSpeedDps < 3.0f) {
        const float correctionWeight = elapsedSeconds / (0.5f + elapsedSeconds);
        upDirection.x += correctionWeight * (accelerationG.x / accelerationMagnitudeG - upDirection.x);
        upDirection.y += correctionWeight * (accelerationG.y / accelerationMagnitudeG - upDirection.y);
        upDirection.z += correctionWeight * (accelerationG.z / accelerationMagnitudeG - upDirection.z);
    }

    // Keep the vector at unit length so it represents direction rather than magnitude.
    const float directionMagnitude = sqrtf(upDirection.x * upDirection.x + upDirection.y * upDirection.y + upDirection.z * upDirection.z);
    upDirection.x /= directionMagnitude;
    upDirection.y /= directionMagnitude;
    upDirection.z /= directionMagnitude;
}

// Separate trigger and reset thresholds prevent repeated warnings near the speed limit.
void check_curl_speed(float curlAngularVelocityDps) {
    constexpr float FAST_SPEED_THRESHOLD_DPS = 120.0f;
    static bool isSpeedWarningActive = false;
    const float curlSpeedDps = fabsf(curlAngularVelocityDps);
    constexpr float SPEED_RESET_THRESHOLD_DPS = 100.0f;

    if (curlSpeedDps > FAST_SPEED_THRESHOLD_DPS && !isSpeedWarningActive) {
        if (Serial) Serial.println("REP TOO FAST");

        isSpeedWarningActive = true;
    } else if (curlSpeedDps < SPEED_RESET_THRESHOLD_DPS) {
        isSpeedWarningActive = false;
    }
}

// Count a rep only after a bottom-to-top-to-bottom sequence.
void update_rep_count(float& curlAngleDegrees, int& repCount, bool isResetRequested) {
    static bool hasStartedAtBottom = false;
    static bool hasReachedTop = false;

    constexpr float BOTTOM_ANGLE_DEGREES = 10.0f;
    constexpr float TOP_ANGLE_DEGREES = 100.0f;

    if (isResetRequested) {
        repCount = 0;
        hasStartedAtBottom = false;
        hasReachedTop = false;
        return;
    }

    if (curlAngleDegrees <= BOTTOM_ANGLE_DEGREES) {
        if (curlAngleDegrees <= 0) curlAngleDegrees = 0;

        if (hasStartedAtBottom && hasReachedTop) ++repCount;

        hasStartedAtBottom = true;
        hasReachedTop = false;
    }

    if (curlAngleDegrees >= TOP_ANGLE_DEGREES) {
        if (curlAngleDegrees >= 120) curlAngleDegrees = 120;

        hasReachedTop = true;
    }
}