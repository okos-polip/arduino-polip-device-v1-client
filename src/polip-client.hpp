/**
 * @file polip-client.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
 * 
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

#ifndef POLIP_CLIENT_HPP
#define POLIP_CLIENT_HPP

//==============================================================================
//  Libraries
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <Crypto.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! Fixed device ingest server URL 
//TODO this should be a domain name not an IP address
#ifndef POLIP_DEVICE_INGEST_SERVER_URL
#define POLIP_DEVICE_INGEST_SERVER_URL      "http://10.0.0.216:3002"
#endif

//! Minimum JSON doc size, larger if state or sense is substatial
#define POLIP_MIN_RECOMMENDED_BUFFER_SIZE   (1024)

//==============================================================================
//  Preprocessor Macros
//==============================================================================

//! Standard format for hardware and firmware version strings
#define POLIP_VERSION_STD_FORMAT(major,minor,patch) ("" #major "." #minor "." #patch)

//==============================================================================
//  Enumerated Constants
//==============================================================================

typedef enum _polip_ret_code {
    POLIP_OK,
    POLIP_ERROR_TAG_MISMATCH,
    POLIP_ERROR_VALUE_MISMATCH,
    POLIP_ERROR_RESPONSE_DESERIALIZATION,
    POLIP_ERROR_SERVER_ERROR
} polip_ret_code_t;

//==============================================================================
//  Data Structure Declaration
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
 * Checks server health check end-point
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_checkServerStatus();
/**
 * Gets the current state of the device from the server
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents)
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_getState(const polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Sets the current state of the device to the server
 * Its recommended to first get state from server before pushing in
 * cases where pending state exists in database but not yet reflected
 * on device.
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain state field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushState(const polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Pushes a notification/error to the server
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain code / message fields
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushError(const polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Pushes sensor state to the server
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushSensors(const polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Gets message identifier value from server (used internally for synchronization)
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_getValue(const polip_device_t* dev, const char* timestamp);

//==============================================================================

#endif /*POLIP_CLIENT_HPP*/