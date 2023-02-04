/**
 * @file binarySwitch.ino
 * @author Curt Henrichs
 * @brief Binary Switch Example
 * @version 0.1
 * @date 2022-10-20
 * 
 * @copyright Copyright (c) 2022
 * 
 */

// Install Arduino_JSON library in Arduino IDE
// Install ESP8266 Board in Arduino IDE
// Install WiFiManager library in Arduino IDE
// Install Arduino Crypto library in Arduino IDE
//      (I had to manually rename the library from Crypto.h to ArduinoCrypto.h)

//==============================================================================
//  Libraries
//==============================================================================

#include <Arduino.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <polip-client.h>

//==============================================================================
//  Preprocessor Constants
//==============================================================================

#define SWITCH_PIN                      (LED_BUILTIN)
#define RESET_BTN_PIN                   (D0)
#define STATUS_LED_PIN                  (D6)

#define RESET_BTN_TIME_THRESHOLD        (100L)
#define POLL_TIME_THRESHOLD             (1000L)

//==============================================================================
//  Preprocessor Macros
//==============================================================================

#define readResetBtnState() ((bool)digitalRead(RESET_BTN_PIN))
#define setSwitchState(state) (digitalWrite(SWITCH_PIN, (bool)state))
#define setStatusLED(state) (digitalWrite(STATUS_LED_PIN, (bool)state))

//==============================================================================
//  Declared Constants
//==============================================================================

const char* SERIAL_STR = "binary-switch-0-0000";
const char* KEY_STR = "revocable-key-0";
const char* HARDWARE_STR = POLIP_VERSION_STD_FORMAT(0,0,0);
const char* FIRMWARE_STR = POLIP_VERSION_STD_FORMAT(0,0,0);

//==============================================================================
//  Private Module Variables
//==============================================================================

static WiFiManager wifiManager;
static polip_device_t polipDevice;

static unsigned long resetTime;
static bool flag_stateChanged = false;
static bool flag_reset = false;
static bool flag_getValue = false;
static bool prevBtnState = false;

static char rxBuffer[50];
static int rxBufferIdx = 0;

static bool currentState = false;
static unsigned long pollTime;

static StaticJsonDocument<512> doc;

//==============================================================================
//  MAIN
//==============================================================================

void setup() {
    Serial.begin(9600);
    Serial.flush();

    pinMode(SWITCH_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(RESET_BTN_PIN, INPUT);

    setSwitchState(currentState);
    setStatusLED(false);

    wifiManager.autoConnect("AP-Polip-Device-Setup");

    polipDevice.serialStr = SERIAL_STR;
    polipDevice.keyStr = (const uint8_t*)KEY_STR;
    polipDevice.keyStrLen = strlen(KEY_STR);
    polipDevice.hardwareStr = HARDWARE_STR;
    polipDevice.firmwareStr = FIRMWARE_STR;

    pollTime = resetTime = millis();

    flag_getValue = false;
    flag_stateChanged = false; 
    flag_reset = false;
    prevBtnState = false;
}

void loop() {
    // We do most things against soft timers in the main loop
    unsigned long currentTime = millis();

    // Push state to sever
    if (flag_stateChanged) {
        const char* timestamp = "";
        doc.clear();
        JsonObject stateObj = doc.createNestedObject("state");
        stateObj["switch"] = currentState;
        polip_ret_code_t polipCode = polip_pushState(&polipDevice, doc, timestamp);

        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            flag_getValue = true;
        } else {
            flag_stateChanged = false;
        }
    }

    // Poll server for state changes
    if (!flag_stateChanged && (currentTime - pollTime) >= POLL_TIME_THRESHOLD) {
        const char* timestamp = "";
        polip_ret_code_t polipCode = polip_getState(&polipDevice, doc, timestamp);

        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            flag_getValue = true;
        } else {
            pollTime = currentTime;
            if (polipCode == POLIP_OK) {
                JsonObject stateObj = doc["state"];
                currentState = stateObj["switch"];
            }
        }
    }

    // Attempt to get sync value from server
    if (flag_getValue) {
        flag_getValue = false;
        const char* timestamp = "";
        polip_getValue(&polipDevice, timestamp);
    }

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
                flag_reset = true;
            } else if (str == "state?") {
                Serial.print(F("STATE = "));
                Serial.println(currentState); 
            } else if (str == "toggle") {
                flag_stateChanged = true;
                currentState = !currentState;
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
            flag_reset = true;
        }
        // Otherwise button press was debounced.
    }
    prevBtnState = btnState;

     // Reset State Machine
    if (flag_reset) {
        flag_reset = false;
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wifiManager.resetSettings();
        ESP.restart();
    }
}