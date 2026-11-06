#include "WebDashboard.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <cstring>
#include <new>

namespace {

constexpr char WIFI_NAME[] = "ExerciseTracker";
constexpr char WIFI_PASSWORD[] = "track1234";

// Serve the website and WebSocket endpoint through the same HTTP server.
AsyncWebServer server(80);
AsyncWebSocket webSocket("/ws");

// Safely pass reset requests from network callbacks to the Arduino loop.
std::atomic<bool> isWebResetPending{false};

constexpr unsigned long WEB_UPDATE_INTERVAL_MS = 200;
unsigned long lastWebUpdateMs = 0;
unsigned long lastCleanupMs = 0;

// Each client has its own buffer because "reset" may arrive across multiple callbacks.
struct ResetCommand {
    char bytes[5]{};
    size_t received = 0;
};

bool isServerStarted = false;
bool isFilesystemReady = false;
bool isReadingReady = false;

float angleDegrees = 0.0f;
float sideAcceleration = 0.0f;
int repetitions = 0;
uint32_t sampleTimeMs = 0;
uint32_t resetId = 0;

OledWarnings setWarnings{};
HeartReadings heartReadings{};

// Serve files from LittleFS without accessing sensors in network callbacks.
void send_web_file(AsyncWebServerRequest* request, const char* path, const char* contentType) {
    if (!isFilesystemReady) {
        request->send(503, "text/plain", "Web files unavailable. Upload the LittleFS filesystem image.");
        return;
    }

    if (!LittleFS.exists(path)) {
        request->send(404, "text/plain", "File missing. Upload the data folder using Upload Filesystem Image.");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, path, contentType);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

void on_websocket_event(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t length) {
    if (type == WS_EVT_CONNECT) {
        // Allocate a command buffer for this client; close if allocation fails.
        client->_tempObject = new (std::nothrow) ResetCommand; 

        if (!client->_tempObject) {
            client->close();
            return;
        }
        
        client->setCloseClientOnQueueFull(false);
        client->keepAlivePeriod(15);

        if (Serial) Serial.println("Dashboard WebSocket connected (/ws)");

    } else if (type == WS_EVT_DISCONNECT) {
        delete static_cast<ResetCommand*>(client->_tempObject);
        client->_tempObject = nullptr;

        if (Serial) Serial.println("Dashboard WebSocket disconnected");

    } else if (type == WS_EVT_DATA) {
        const auto* frame = static_cast<AwsFrameInfo*>(arg);
        auto* command = static_cast<ResetCommand*>(client->_tempObject);

        if (!command || !frame) return;

        // Accept one five-byte text frame, allowing its bytes to arrive in separate TCP deliveries.
        if (frame->index == 0) command->received = 0;

        if (!frame->final || frame->num != 0 || frame->opcode != WS_TEXT ||
            frame->len != 5 || frame->index != command->received || length > 5 - command->received) {
            command->received = 0;
            return;
        }

        std::memcpy(command->bytes + command->received, data, length);
        command->received += length;

        if (command->received == 5) {
            if (std::memcmp(command->bytes, "reset", 5) == 0) isWebResetPending.store(true);

            command->received = 0;
        }
    }
}

// Build a JSON snapshot in the Arduino loop and send it to connected browsers.
void send_readings() {
    char response[640];

    const int length = snprintf(
        response, sizeof(response),
        "{\"ready\":%s,\"sampleMs\":%lu,\"resetId\":%lu,"
        "\"angle\":%.2f,\"reps\":%d,\"sideAcceleration\":%.3f,"
        "\"tooFast\":%s,\"excessiveSway\":%s,\"heartConnected\":%s,"
        "\"fingerPresent\":%s,\"bpmAvailable\":%s,\"bpm\":%.1f,\"swayThreshold\":3.0}",
        isReadingReady ? "true" : "false",
        static_cast<unsigned long>(sampleTimeMs),
        static_cast<unsigned long>(resetId),
        angleDegrees, repetitions, sideAcceleration,
        setWarnings.hasSpeedWarning ? "true" : "false",
        setWarnings.hasSwayWarning ? "true" : "false",
        heartReadings.isConnected ? "true" : "false",
        heartReadings.isFingerPresent ? "true" : "false",
        heartReadings.isBpmAvailable ? "true" : "false",
        heartReadings.heartRateBpm
    );

    // Send only if formatting succeeded and the complete message fits the buffer.
    if (length > 0 && static_cast<size_t>(length) < sizeof(response)) {
        webSocket.textAll(response, static_cast<size_t>(length));
    }
}

} // namespace

// Start the device's Wi-Fi network and register the website and WebSocket routes.
void initialize_web_dashboard() {
    // Do not automatically format storage if mounting fails.
    isFilesystemReady = LittleFS.begin(false);

    // Stop any previously running access point before applying new credentials.
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP);

    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

    if (!WiFi.softAP(WIFI_NAME, WIFI_PASSWORD)) {
        if (Serial) Serial.println("Could not start dashboard Wi-Fi");
        return;
    }

    if (Serial) {
        Serial.printf("Wi-Fi started: %s\n", WiFi.softAPSSID().c_str());
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_web_file(request, "/index.html", "text/html; charset=utf-8");
    });

    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_web_file(request, "/index.html", "text/html; charset=utf-8");
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_web_file(request, "/style.css", "text/css; charset=utf-8");
    });

    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_web_file(request, "/script.js", "application/javascript; charset=utf-8");
    });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    webSocket.onEvent(on_websocket_event);
    server.addHandler(&webSocket);
    server.begin();

    isServerStarted = true;

    if (Serial) Serial.printf("Dashboard: connect to %s, then open http://192.168.4.1\n", WIFI_NAME);
}

void handle_web_dashboard() {
    if (!isServerStarted) return;

    // Atomically read and clear the pending request, then apply it in the Arduino loop.
    if (isWebResetPending.exchange(false)) isSetDone = true;

    const unsigned long now = millis();

    if (now - lastCleanupMs >= 1000) {
        lastCleanupMs = now;
        webSocket.cleanupClients(4);
    }

    if (now - lastWebUpdateMs >= WEB_UPDATE_INTERVAL_MS) {
        lastWebUpdateMs = now;
        if (webSocket.count() > 0) send_readings();
    }
}

// Store the latest snapshot, replacing non-finite values that JSON cannot represent.
void update_web_readings(float curlAngleDegrees, int repCount, float sideAccelerationMps2, const OledWarnings& warnings, const HeartReadings& heart, bool isSetReset) {
    angleDegrees = std::isfinite(curlAngleDegrees) ? curlAngleDegrees : 0.0f;
    sideAcceleration = std::isfinite(sideAccelerationMps2) ? sideAccelerationMps2 : 0.0f;
    repetitions = repCount;
    setWarnings = warnings;
    heartReadings = heart;

    if (!std::isfinite(heartReadings.heartRateBpm)) {
        heartReadings.heartRateBpm = 0.0f;
        heartReadings.isBpmAvailable = false;
    }

    sampleTimeMs = millis();

    // A changed reset ID lets the browser recognize resets from either input.
    if (isSetReset) ++resetId;

    isReadingReady = true;
}