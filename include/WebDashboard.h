#pragma once

// Provide the OledWarnings and HeartReadings types.
#include "header.h"

// Start the Wi-Fi access point and web server for the dashboard.
void initialize_web_dashboard();

// Handle dashboard communication, incoming reset requests,
// and periodic transmission of sensor readings.
void handle_web_dashboard();

// Update the readings shared with the dashboard.
// isSetReset indicates that a set reset occurred.
void update_web_readings(
    float curlAngleDegrees,
    int repCount,
    float sideAccelerationMps2,
    const OledWarnings& warnings,
    const HeartReadings& heart,
    bool isSetReset
);