/**
 * @file polip-device.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

#ifndef POLIP_DEVICE_HPP
#define POLIP_DEVICE_HPP

//==============================================================================
//  Libraries
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ArduinoCrypto.h>
#include <ESP8266HTTPClient.h>

#include "./polip-core.hpp"

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! Fixed device ingest server URL
#ifndef POLIP_DEVICE_INGEST_SERVER_URL
#define POLIP_DEVICE_INGEST_SERVER_URL              "http://api.okospolip.com:3021"
#endif
// PORTs
//   3010 - internal schema
//   3011 - external schema http
//   3012 - external schema https
//   3020 - internal ingest v1
//   3021 - external ingest v1 http
//   3033 - external ingest v1 https

//! Minimum JSON doc size, larger if state or sense is substatial
#ifndef POLIP_MIN_RECOMMENDED_DOC_SIZE
#define POLIP_MIN_RECOMMENDED_DOC_SIZE              (1024)
#endif

//! Minimum buffer size, larger if state or sense is substatial
#ifndef POLIP_ARBITRARY_MSG_BUFFER_SIZE
#define POLIP_ARBITRARY_MSG_BUFFER_SIZE             (512)
#endif

//! Buffer size needed to construct URI's with parameters
#ifndef POLIP_QUERY_URI_BUFFER_SIZE
#define POLIP_QUERY_URI_BUFFER_SIZE                 (128)
#endif

//==============================================================================
//  Preprocessor Macros
//==============================================================================

#define POLIP_BLOCK_AWAIT_SERVER_OK() {                                         \
    Serial.println(F("Connecting to Okos Polip Device Ingest Service"));        \
    bool wait = true;                                                           \
    while (wait) {                                                              \
        wait = (POLIP_OK != polip_checkServerStatus());                         \
        if (wait) {                                                             \
            Serial.println(F("Failed to connect. Retrying..."));                \
            delay(500);                                                         \
        }                                                                       \
    }                                                                           \
    Serial.println(F("Connected"));                                             \
}

#define polip_pushNotification(dev, doc, timestamp) (                           \
    polip_pushError(                                                            \
        dev,                                                                    \
        doc,                                                                    \
        timestamp                                                               \
    )                                                                           \
)

//==============================================================================
//  Public Data Structure Declaration
//==============================================================================

/**
 * Defines all necessary meta-data to establish communication with server
 * Application code must setup all strings / parameters according to spec 
 * in Okos Polip database
 */
typedef struct _polip_device {  
    uint32_t value = 0;             //! Incremented value for next transmission id
    bool skipTagCheck = false;      //! Set true if key -> tag gen not needed
    const char* serialStr = NULL;   //! Serial identifier unique to this device
    const uint8_t* keyStr = NULL;   //! Revocable key used for tag gen
    int keyStrLen = 0;              //! Length of key buffer
    const char* hardwareStr = NULL; //! Hardware version to report to server
    const char* firmwareStr = NULL; //! Firmware version to report to server
} polip_device_t;

//==============================================================================
//  Public Function Prototypes
//==============================================================================

/**
 * @brief Checks server health check end-point
 * 
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_checkServerStatus();
/**
 * @brief Gets the current state of the device from the server
 * 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents)
 * @param timestamp pointer to formated timestamp string
 * @param queryState boolean (default true) additionally queries for state data
 * @param queryManufacturer boolean (default false) additionally queries for manufacturer defined data
 * @param queryRPC boolean (default false) additionally queries for pending rpcs
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success 
 */
polip_ret_code_t polip_getState(polip_device_t* dev, JsonDocument& doc, const char* timestamp, 
        bool queryState = true, bool queryManufacturer = false, bool queryRPC = false);
/**
 * @brief Gets the current metadata state of the device from server
 * 
 * @param dev pointer to device
 * @param doc reference to JSON buffer (will clear/replace contents)
 * @param timestamp pointer to formatted timestamp string
 * @param queryState boolean (default true) queries for state metadata
 * @param querySensors boolean (default true) queuries for sensor metadata
 * @param queryManufacturer boolean (default true) queries for manufacturer defined data
 * @param queryGeneral boolean (default true) queries for general metadata
 * @return polip_ret_code_t 
 */
polip_ret_code_t polip_getMeta(polip_device_t* dev, JsonDocument& doc, const char* timestamp,
        bool queryState = true, bool querySensors = true, bool queryManufacturer = true, bool queryGeneral = true);
/**
 * @brief Sets the current state of the device to the server
 * Its recommended to first get state from server before pushing in
 * cases where pending state exists in database but not yet reflected
 * on device.
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain state field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushState(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Pushes a notification/error to the server
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain code / message fields
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushError(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Pushes sensor state to the server
 * 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushSensors(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Gets message identifier value from server (used internally for synchronization)
 * 
 * @param client is reference to external 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_getValue(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Pushes RPC response to the server
 * 
 * @param client is reference to external 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain rpc field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushRPC(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Gets schema for this specific device
 * 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success 
 */
polip_ret_code_t polip_getSchema(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Gets semantic JSON table for all error codes
 * 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success 
 */
polip_ret_code_t polip_getAllErrorSemantics(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * @brief Gets semantic JSON table for code supplied
 * 
 * @param dev pointer to device 
 * @param code integer to lookup semantic
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success 
 */
polip_ret_code_t polip_getErrorSemanticFromCode(polip_device_t* dev, int32_t code, JsonDocument& doc, const char* timestamp);

//==============================================================================

#endif /*POLIP_DEVICE_HPP*/