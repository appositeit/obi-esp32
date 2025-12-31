/**
 * Open Battery Information - ESP32-C3 Firmware
 *
 * FUNCTIONAL REQUIREMENTS:
 * 1. Serial bridge mode: Communicate with PC via USB serial using OBI protocol
 * 2. Web server mode: Browser-based interface for standalone diagnostics
 * 3. Support Makita LXT 18V battery protocol via modified OneWire
 * 4. WiFi configuration via captive portal on first boot
 *
 * HARDWARE CONFIGURATION:
 * - ESP32-C3 Super Mini
 * - GPIO3: OneWire data line (4.7kΩ pull-up to 3.3V)
 * - GPIO4: Enable pin (4.7kΩ pull-up to 3.3V)
 * - Battery Pin 2: OneWire data
 * - Battery Pin 6: Enable (must be HIGH during communication)
 *
 * MODES:
 * - Build with -DENABLE_WEB_SERVER=1 for standalone web interface
 * - Default build provides serial bridge compatible with Python GUI
 *
 * PROTOCOL (Serial Bridge):
 * Request:  [0x01][data_len][rsp_len][cmd][data...]
 * Response: [cmd][rsp_len][data...]
 *
 * AI-generated on 2025-12-16
 */

#include <Arduino.h>
#include "OneWire2.h"

#ifdef ENABLE_WEB_SERVER
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "web_interface.h"
#endif

// Version
#define OBI_VERSION_MAJOR 1
#define OBI_VERSION_MINOR 0
#define OBI_VERSION_PATCH 0

// Pin definitions (can be overridden via build flags)
#ifndef ONEWIRE_PIN
#define ONEWIRE_PIN 3
#endif

#ifndef ENABLE_PIN
#define ENABLE_PIN 4
#endif

// WiFi credentials (for web server mode)
#ifndef WIFI_SSID
#define WIFI_SSID "YourSSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "YourPassword"
#endif

// Nibble swap helper
#define SWAP_NIBBLES(x) (((x) & 0x0F) << 4 | ((x) & 0xF0) >> 4)

// Instantiate OneWire with template pin
OneWire<ONEWIRE_PIN> makita;

#ifdef ENABLE_WEB_SERVER
WebServer server(80);
#endif

// Battery data structure
struct BatteryData {
    bool valid;
    char model[16];
    bool locked;
    uint16_t chargeCount;
    char mfgDate[16];
    float capacity;
    uint8_t errorCode;
    uint8_t romId[8];
    float packVoltage;
    float cellVoltage[5];
    float cellDiff;
    float tempCell;
    float tempMosfet;
};

BatteryData batteryData;

// Forward declarations
void processSerialCommand();
bool cmdAndRead33(byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len);
bool cmdAndReadCC(byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len);
void sendUSB(byte *rsp, byte rsp_len);
void setEnable(bool high);
void triggerPower();
bool readBatteryInfo();
bool readBatteryVoltages();
bool readBatteryModel();

#ifdef ENABLE_WEB_SERVER
void setupWebServer();
void setupOTA();
#endif

// ------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Wait for serial connection (USB CDC)
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    // Configure pins
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);

    // Initialise battery data
    memset(&batteryData, 0, sizeof(batteryData));

    Serial.println("=================================");
    Serial.println("OBI ESP32-C3 - Open Battery Info");
    Serial.println("=================================");
    Serial.printf("Version: %d.%d.%d\n", OBI_VERSION_MAJOR, OBI_VERSION_MINOR, OBI_VERSION_PATCH);
    Serial.printf("OneWire Pin: GPIO%d\n", ONEWIRE_PIN);
    Serial.printf("Enable Pin: GPIO%d\n", ENABLE_PIN);

#ifdef ENABLE_WEB_SERVER
    Serial.println("Mode: Web Server + Serial Bridge");
    Serial.println("Connecting to WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        setupOTA();
        setupWebServer();
    } else {
        Serial.println();
        Serial.println("WiFi failed - Serial bridge only");
    }
#else
    Serial.println("Mode: Serial Bridge Only");
#endif

    Serial.println("Ready.");
}

// ------------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------------
void loop() {
#ifdef ENABLE_WEB_SERVER
    ArduinoOTA.handle();
    server.handleClient();
#endif
    processSerialCommand();
}

// ------------------------------------------------------------------
// Enable pin control
// ------------------------------------------------------------------
void setEnable(bool high) {
    digitalWrite(ENABLE_PIN, high ? HIGH : LOW);
}

void triggerPower() {
    setEnable(false);
    delay(200);
    setEnable(true);
    delay(500);
}

// ------------------------------------------------------------------
// OneWire command functions
// ------------------------------------------------------------------

bool cmdAndRead33(byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len) {
    int i;

    for (int retry = 0; retry < 3; retry++) {
        if (!makita.reset()) {
            triggerPower();
            continue;
        }

        delayMicroseconds(310);
        makita.write(0x33);

        // Read 8-byte ROM ID
        for (i = 0; i < 8; i++) {
            delayMicroseconds(90);
            rsp[i] = makita.read();
        }

        // Write command
        for (i = 0; i < cmd_len; i++) {
            delayMicroseconds(90);
            makita.write(cmd[i]);
        }

        // Read response
        for (i = 8; i < rsp_len + 8; i++) {
            delayMicroseconds(90);
            rsp[i] = makita.read();
        }

        // Check if valid (not all 0xFF)
        bool valid = false;
        for (i = 0; i < rsp_len + 8; i++) {
            if (rsp[i] != 0xFF) {
                valid = true;
                break;
            }
        }
        if (valid) return true;
    }

    memset(rsp, 0xFF, rsp_len + 8);
    return false;
}

bool cmdAndReadCC(byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len) {
    int i;

    for (int retry = 0; retry < 3; retry++) {
        if (!makita.reset()) {
            triggerPower();
            continue;
        }

        delayMicroseconds(310);
        makita.write(0xCC);

        // Write command
        for (i = 0; i < cmd_len; i++) {
            delayMicroseconds(90);
            makita.write(cmd[i]);
        }

        // Read response
        for (i = 0; i < rsp_len; i++) {
            delayMicroseconds(90);
            rsp[i] = makita.read();
        }

        // Check if valid
        bool valid = false;
        for (i = 0; i < rsp_len; i++) {
            if (rsp[i] != 0xFF) {
                valid = true;
                break;
            }
        }
        if (valid) return true;
    }

    memset(rsp, 0xFF, rsp_len);
    return false;
}

// ------------------------------------------------------------------
// High-level battery functions
// ------------------------------------------------------------------

bool readBatteryInfo() {
    byte rsp[48];
    byte cmd[] = {0xAA, 0x00};

    setEnable(true);
    delay(400);

    bool success = cmdAndRead33(cmd, 2, rsp, 40);

    if (success) {
        // Copy ROM ID
        memcpy(batteryData.romId, rsp, 8);

        // Parse message data (offset by 8 for ROM ID)
        byte *msg = &rsp[8];

        // Manufacturing date is in ROM ID bytes - use ISO 8601 (YYYY-MM-DD)
        // romId[0] = year, romId[1] = month, romId[2] = day
        snprintf(batteryData.mfgDate, sizeof(batteryData.mfgDate),
                 "20%02d-%02d-%02d",
                 batteryData.romId[0],   // year
                 batteryData.romId[1],   // month
                 batteryData.romId[2]);  // day

        // Charge count
        uint16_t rawCount = ((uint16_t)SWAP_NIBBLES(msg[29])) |
                           (((uint16_t)SWAP_NIBBLES(msg[28])) << 8);
        batteryData.chargeCount = rawCount & 0x0FFF;

        // Lock status
        batteryData.locked = (msg[22] & 0x0F) > 0;

        // Error code
        batteryData.errorCode = msg[21] & 0x0F;

        // Capacity
        batteryData.capacity = SWAP_NIBBLES(msg[18]) / 10.0f;

        batteryData.valid = true;
    }

    setEnable(false);
    return success;
}

bool readBatteryModel() {
    byte rsp[16];
    byte cmd[] = {0xDC, 0x0C};

    setEnable(true);
    delay(400);

    bool success = cmdAndReadCC(cmd, 2, rsp, 10);

    if (success && rsp[0] != 0xFF) {
        // Copy model string (null-terminate)
        memcpy(batteryData.model, rsp, 7);
        batteryData.model[7] = '\0';
    } else {
        // Try F0513 method for older batteries
        makita.reset();
        delayMicroseconds(400);
        makita.write(0xCC);
        delayMicroseconds(90);
        makita.write(0x99);
        delay(400);
        makita.reset();
        delayMicroseconds(400);
        makita.write(0x31);
        delayMicroseconds(90);
        byte b1 = makita.read();
        delayMicroseconds(90);
        byte b0 = makita.read();

        if (b0 != 0xFF && b1 != 0xFF) {
            snprintf(batteryData.model, sizeof(batteryData.model), "BL%02X%02X", b1, b0);
            success = true;
        }
    }

    setEnable(false);
    return success;
}

bool readBatteryVoltages() {
    byte rsp[32];
    byte cmd[] = {0xD7, 0x00, 0x00, 0xFF};

    setEnable(true);
    delay(400);

    bool success = cmdAndReadCC(cmd, 4, rsp, 29);

    if (success && rsp[0] != 0xFF) {
        batteryData.packVoltage = ((uint16_t)rsp[0] | ((uint16_t)rsp[1] << 8)) / 1000.0f;

        float maxV = 0, minV = 5;
        for (int i = 0; i < 5; i++) {
            float v = ((uint16_t)rsp[2 + i*2] | ((uint16_t)rsp[3 + i*2] << 8)) / 1000.0f;
            batteryData.cellVoltage[i] = v;
            if (v > maxV) maxV = v;
            if (v < minV) minV = v;
        }
        batteryData.cellDiff = maxV - minV;

        // Cell temperature (offset 14-15)
        int16_t tempRaw = (int16_t)rsp[14] | ((int16_t)rsp[15] << 8);
        batteryData.tempCell = tempRaw / 100.0f;

        // MOSFET temperature (offset 16-17)
        int16_t tempMosfetRaw = (int16_t)rsp[16] | ((int16_t)rsp[17] << 8);
        batteryData.tempMosfet = tempMosfetRaw / 100.0f;
    } else {
        // Try F0513 method
        byte vcmd[1];
        bool f0513_ok = true;

        for (int i = 0; i < 5 && f0513_ok; i++) {
            vcmd[0] = 0x31 + i;
            if (cmdAndReadCC(vcmd, 1, rsp, 2)) {
                batteryData.cellVoltage[i] = ((uint16_t)rsp[0] | ((uint16_t)rsp[1] << 8)) / 1000.0f;
            } else {
                f0513_ok = false;
            }
        }

        if (f0513_ok) {
            // Calculate pack voltage and diff
            float sum = 0, maxV = 0, minV = 5;
            for (int i = 0; i < 5; i++) {
                sum += batteryData.cellVoltage[i];
                if (batteryData.cellVoltage[i] > maxV) maxV = batteryData.cellVoltage[i];
                if (batteryData.cellVoltage[i] < minV) minV = batteryData.cellVoltage[i];
            }
            batteryData.packVoltage = sum;
            batteryData.cellDiff = maxV - minV;

            // Temperature (F0513 only has cell temp, no MOSFET temp)
            vcmd[0] = 0x52;
            if (cmdAndReadCC(vcmd, 1, rsp, 2)) {
                batteryData.tempCell = ((uint16_t)rsp[0] | ((uint16_t)rsp[1] << 8)) / 100.0f;
                batteryData.tempMosfet = 0;  // Not available on F0513
            }
            success = true;
        }
    }

    setEnable(false);
    return success;
}

// ------------------------------------------------------------------
// Serial communication (OBI Protocol)
// ------------------------------------------------------------------

void sendUSB(byte *rsp, byte rsp_len) {
    for (int i = 0; i < rsp_len; i++) {
        Serial.write(rsp[i]);
    }
}

void processSerialCommand() {
    if (Serial.available() >= 4) {
        byte start = Serial.read();
        byte len;
        byte rsp_len;
        byte cmd;
        byte data[255];
        byte rsp[255];

        if (start != 0x01) {
            return;
        }

        len = Serial.read();
        rsp_len = Serial.read();
        cmd = Serial.read();

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                while (Serial.available() < 1) {}
                data[i] = Serial.read();
            }
        }

        setEnable(true);
        delay(400);

        switch (cmd) {
            case 0x01:
                rsp[0] = 0x01;
                rsp[1] = rsp_len;
                rsp[2] = OBI_VERSION_MAJOR;
                rsp[3] = OBI_VERSION_MINOR;
                rsp[4] = OBI_VERSION_PATCH;
                break;

            case 0x31:
            case 0x32: {
                makita.reset();
                delayMicroseconds(400);
                makita.write(0xCC);
                delayMicroseconds(90);
                makita.write(0x99);
                delay(400);
                makita.reset();
                delayMicroseconds(400);
                makita.write(cmd);
                delayMicroseconds(90);
                rsp[3] = makita.read();
                delayMicroseconds(90);
                rsp[2] = makita.read();
                break;
            }

            case 0x33:
                cmdAndRead33(data, len, &rsp[2], rsp_len);
                break;

            case 0xCC:
                cmdAndReadCC(data, len, &rsp[2], rsp_len);
                break;

            default:
                rsp_len = 0;
                break;
        }

        rsp[0] = cmd;
        rsp[1] = rsp_len;
        sendUSB(rsp, rsp_len + 2);

        setEnable(false);
    }
}

// ------------------------------------------------------------------
// OTA Updates
// ------------------------------------------------------------------

#ifdef ENABLE_WEB_SERVER
void setupOTA() {
    ArduinoOTA.setHostname("obi-esp32");

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("OTA Start: " + type);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready");
}
#endif

// ------------------------------------------------------------------
// Web Server (Phase 2)
// ------------------------------------------------------------------

#ifdef ENABLE_WEB_SERVER
void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiRead() {
    readBatteryInfo();
    readBatteryModel();
    readBatteryVoltages();

    JsonDocument doc;
    doc["success"] = batteryData.valid;
    doc["model"] = batteryData.model;
    doc["locked"] = batteryData.locked;
    doc["chargeCount"] = batteryData.chargeCount;
    doc["mfgDate"] = batteryData.mfgDate;
    doc["capacity"] = batteryData.capacity;
    doc["errorCode"] = batteryData.errorCode;
    doc["packVoltage"] = batteryData.packVoltage;
    doc["cell1"] = batteryData.cellVoltage[0];
    doc["cell2"] = batteryData.cellVoltage[1];
    doc["cell3"] = batteryData.cellVoltage[2];
    doc["cell4"] = batteryData.cellVoltage[3];
    doc["cell5"] = batteryData.cellVoltage[4];
    doc["cellDiff"] = batteryData.cellDiff;
    doc["tempCell"] = batteryData.tempCell;
    doc["tempMosfet"] = batteryData.tempMosfet;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiVoltages() {
    bool success = readBatteryVoltages();

    JsonDocument doc;
    doc["success"] = success;
    doc["packVoltage"] = batteryData.packVoltage;
    doc["cell1"] = batteryData.cellVoltage[0];
    doc["cell2"] = batteryData.cellVoltage[1];
    doc["cell3"] = batteryData.cellVoltage[2];
    doc["cell4"] = batteryData.cellVoltage[3];
    doc["cell5"] = batteryData.cellVoltage[4];
    doc["cellDiff"] = batteryData.cellDiff;
    doc["tempCell"] = batteryData.tempCell;
    doc["tempMosfet"] = batteryData.tempMosfet;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiLeds() {
    bool state = server.hasArg("state") && server.arg("state") == "1";

    setEnable(true);
    delay(400);

    // Test mode command
    byte cmd1[] = {0xD9, 0x96, 0xA5};
    byte rsp[32];
    cmdAndRead33(cmd1, 3, rsp, 9);

    // LED command
    byte cmd2[] = {0xDA, (byte)(state ? 0x31 : 0x34)};
    cmdAndRead33(cmd2, 2, rsp, 9);

    setEnable(false);

    server.send(200, "application/json", "{\"success\":true}");
}

void handleApiReset() {
    setEnable(true);
    delay(400);

    // Test mode
    byte cmd1[] = {0xD9, 0x96, 0xA5};
    byte rsp[32];
    cmdAndRead33(cmd1, 3, rsp, 9);

    // Reset error
    byte cmd2[] = {0xDA, 0x04};
    cmdAndRead33(cmd2, 2, rsp, 9);

    setEnable(false);

    server.send(200, "application/json", "{\"success\":true}");
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/read", HTTP_GET, handleApiRead);
    server.on("/api/voltages", HTTP_GET, handleApiVoltages);
    server.on("/api/leds", HTTP_GET, handleApiLeds);
    server.on("/api/reset", HTTP_GET, handleApiReset);

    server.begin();
    Serial.println("Web server started on port 80");
}
#endif
