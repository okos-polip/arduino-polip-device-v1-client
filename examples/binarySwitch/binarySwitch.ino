/**
 * @file binarySwitch.ino
 * @author Curt Henrichs
 * @brief Binary Switch Example
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Demonstrates core polip-lib behavior using a binary switch type.
 * This could directly control an AC power outlet for instance.
 * 
 * Dependency installation:
 * ---
 * - Arduino_JSON (via Arduino IDE)
 * - ESP8266 Board (via Arduino IDE)
 * - WiFiManager (via Arduino IDE)
 * - NTPClient (via Arduino IDE)
 * - Arduino Crypto (via Arduino IDE)
 *      - (I had to manually rename the library from Crypto.h to ArduinoCrypto.h)
 * 
 * Debug Serial:
 * ---
 * Several commands available for testing behavior
 * 
 *  [reset] -> Requests hardware state reset (will wipe EEPROM)
 *  [state?] -> Queries current state code
 *  [toggle] -> Flips current state boolean (power state)
 *  [error?] -> Queries error status of polip lib
 */

//==============================================================================
//  Libraries
//==============================================================================

#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <polip-client.h>
#include <ESP8266HTTPClient.h>

//==============================================================================
//  Preprocessor Constants
//==============================================================================

#define SWITCH_PIN                      (LED_BUILTIN)
#define RESET_BTN_PIN                   (D0)

#define RESET_BTN_TIME_THRESHOLD        (200L)
#define POLL_TIME_THRESHOLD             (1000L)

#define NTP_URL                         "pool.ntp.org"

#define DEBUG_SERIAL_BAUD               (115200)

//==============================================================================
//  Preprocessor Macros
//==============================================================================

#define readResetBtnState() ((bool)digitalRead(RESET_BTN_PIN))
#define setSwitchState(state) (digitalWrite(SWITCH_PIN, (bool)state))   

//==============================================================================
//  Declared Constants
//==============================================================================

const char* SERIAL_STR = "binary-switch-0-0000";
const char* KEY_STR = "revocable-key-0";    //NOTE: Should be configurable
const char* HARDWARE_STR = POLIP_VERSION_STD_FORMAT(0,0,1);
const char* FIRMWARE_STR = POLIP_VERSION_STD_FORMAT(0,0,1);

//==============================================================================
//  Private Module Variables
//==============================================================================

static StaticJsonDocument<512> doc;
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP, NTP_URL, 0);
static WiFiManager wifiManager;
static polip_device_t polipDevice;
static polip_workflow_t polipWorkflow;

static unsigned long resetTime;
static bool flag_reset = false;
static bool prevBtnState = false;
static char rxBuffer[50];
static int rxBufferIdx = 0;
static bool currentState = false;
static unsigned long pollTime;

//==============================================================================
//  Private Function Prototypes
//==============================================================================

static void _pushStateSetup(polip_device_t* dev, JsonDocument& state);
static void _pollStateResponse(polip_device_t* dev, JsonDocument& state);
static void _errorHandler(polip_device_t* dev, JsonDocument& state, polip_workflow_source_t source);

//==============================================================================
//  MAIN
//==============================================================================

void setup() {
    Serial.begin(DEBUG_SERIAL_BAUD);
    Serial.flush();

    pinMode(SWITCH_PIN, OUTPUT);
    pinMode(RESET_BTN_PIN, INPUT);
    setSwitchState(currentState);

    wifiManager.autoConnect("AP-Polip-Device-Setup");

    timeClient.begin();

    POLIP_BLOCK_AWAIT_SERVER_OK();

    polipDevice.serialStr = SERIAL_STR;
    polipDevice.keyStr = (const uint8_t*)KEY_STR;
    polipDevice.keyStrLen = strlen(KEY_STR);
    polipDevice.hardwareStr = HARDWARE_STR;
    polipDevice.firmwareStr = FIRMWARE_STR;
    polipDevice.skipTagCheck = false;

    polipWorkflow.device = &polipDevice;
    polipWorkflow.hooks.pushStateSetupCb = _pushStateSetup;
    polipWorkflow.hooks.pollStateRespCb = _pollStateResponse;
    polipWorkflow.hooks.workflowErrorCb = _errorHandler;

    unsigned long currentTime = millis();
    polip_workflow_initialize(&polipWorkflow, currentTime);
    pollTime = resetTime = currentTime;
    flag_reset = false;
    prevBtnState = false;
}

void loop() {
    // We do most things against soft timers in the main loop
    unsigned long currentTime = millis();

    // Refresh time
    timeClient.update();

    // Update Polip Server
    polip_workflow_periodic_update(&polipWorkflow, doc, timeClient.getFormattedTime().c_str(), currentTime);

    // Serial debugging interface provides full state control
    while (Serial.available() > 0) {
        if (rxBufferIdx >= (sizeof(rxBuffer) - 1)) {
            Serial.println(F("Error - Buffer Overflow ~ Clearing"));
            rxBufferIdx = 0;
        }

        char c = Serial.read();
        if (c != '\n') {
            rxBuffer[rxBufferIdx] = c;
            rxBufferIdx++;
        } else {
            rxBuffer[rxBufferIdx] = '\0';
            rxBufferIdx = 0;

            String str = String(rxBuffer);

            if (str == "reset") {
                Serial.println(F("Debug Reset Requested"));
                flag_reset = true;
            } else if (str == "state?") {
                Serial.print(F("STATE = "));
                Serial.println(currentState); 
            } else if (str == "toggle") {
                POLIP_WORKFLOW_STATE_CHANGED(&polipWorkflow);
                currentState = !currentState;
            } else if (str == "error?") {
                if (POLIP_WORKFLOW_IN_ERROR(&polipWorkflow)) {
                    Serial.println(F("Error in PolipLib: "));
                    Serial.println((int)polipWorkflow.flags.error);
                } else {
                    Serial.println(F("No Error"));
                }
                POLIP_WORKFLOW_ACK_ERROR(&polipWorkflow);
            } else {
                Serial.print(F("Error - Invalid Command ~ `"));
                Serial.print(str);
                Serial.println(F("`"));
            }
        }
    }

    // Handle Reset button
    bool btnState = readResetBtnState();
    if (!btnState && prevBtnState) {
        resetTime = currentTime;
    } else if (btnState && !prevBtnState) {
        if ((currentTime - resetTime) >= RESET_BTN_TIME_THRESHOLD) {
            Serial.println(F("Reset Button Held"));
            flag_reset = true;
        }
        // Otherwise button press was debounced.
    }
    prevBtnState = btnState;

     // Reset State Machine
    if (flag_reset) {
        flag_reset = false;
        Serial.println(F("Erasing Config, restarting"));
        wifiManager.resetSettings();
        ESP.restart();
    }

    // Update physical state
    setSwitchState(currentState);
    delay(1);
}

//==============================================================================
//  Private Function Implementation
//==============================================================================

static void _pushStateSetup(polip_device_t* dev, JsonDocument& state) {
    JsonObject stateObj = doc.createNestedObject("state");
    stateObj["power"] = currentState;
}

static void _pollStateResponse(polip_device_t* dev, JsonDocument& state) {
    JsonObject stateObj = doc["state"];
    currentState = stateObj["power"];
}

static void _errorHandler(polip_device_t* dev, JsonDocument& state, polip_workflow_source_t source) {
    Serial.print(F("Error Handler ~ polip server error during OP="));
    Serial.print((int)source);
    Serial.print(F(" with CODE="));
    Serial.println((int)polipWorkflow.flags.error);
} 