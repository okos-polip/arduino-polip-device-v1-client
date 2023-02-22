/**
 * @file polip-client.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
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
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ArduinoCrypto.h>
#include <ESP8266HTTPClient.h>

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! Fixed device ingest server URL 
//TODO this should be a domain name not an IP address
#ifndef POLIP_DEVICE_INGEST_SERVER_URL
#define POLIP_DEVICE_INGEST_SERVER_URL              "http://10.0.0.216:3020"
#endif

//! Minimum JSON doc size, larger if state or sense is substatial
#ifndef POLIP_MIN_RECOMMENDED_DOC_SIZE
#define POLIP_MIN_RECOMMENDED_DOC_SIZE              (1024)
#endif

//! Allows POLIP lib to print out debug information on serial bus
#ifndef POLIP_VERBOSE_DEBUG
#define POLIP_VERBOSE_DEBUG                         (false)
#endif

//! Minimum buffer size, larger if state or sense is substatial
#ifndef POLIP_ARBITRARY_MSG_BUFFER_SIZE
#define POLIP_ARBITRARY_MSG_BUFFER_SIZE             (512)
#endif

//! Periodic poll of server device state
#ifndef POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD
#define POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD     (1000L)
#endif

//! Periodic oush of device sensors
#ifndef POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD
#define POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD     (1000L)
#endif

//==============================================================================
//  Preprocessor Macros
//==============================================================================

#ifndef F
#define F(arg) (arg)
#endif

/**
 * Standard format for hardware and firmware version strings 
 */
#define POLIP_VERSION_STD_FORMAT(major,minor,patch) ("v" #major "." #minor "." #patch)

#define POLIP_WORKFLOW_STATE_CHANGED(workflowPtr) {                             \
    (workflowPtr)->flags.stateChanged = true;                                   \
}

#define POLIP_WORKFLOW_SENSE_CHANGED(workflowPtr) {                             \
    (workflowPtr)->flags.senseChanged = true;                                   \
}

#define POLIP_WORKFLOW_IN_ERROR(workflowPtr) ((workflowPtr)->flags.error != POLIP_OK)

#define POLIP_WORKFLOW_ACK_ERROR(workflowPtr) {                                 \
    (workflowPtr)->flags.error = POLIP_OK;                                      \
}

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

//==============================================================================
//  Enumerated Constants
//==============================================================================

/**
 * Errors generated during polip function operation (comprehensive)
 * Not all routines will generate all errors.
 */
typedef enum _polip_ret_code {
    POLIP_OK,
    POLIP_ERROR_TAG_MISMATCH,
    POLIP_ERROR_VALUE_MISMATCH,
    POLIP_ERROR_RESPONSE_DESERIALIZATION,
    POLIP_ERROR_SERVER_ERROR,
    POLIP_ERROR_LIB_REQUEST,
    POLIP_ERROR_WORKFLOW
} polip_ret_code_t;

/**
 * Workflow routines
 */
typedef enum _polip_workflow_source {
    POLIP_WORKFFLOW_PUSH_STATE,
    POLIP_WORKFLOW_POLL_STATE,
    POLIP_WORKFLOW_GET_VALUE,
    POLIP_WORKFLOW_PUSH_SENSE
} polip_workflow_source_t;

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

/**
 * 
 */
struct _polip_workflow_params {
    bool onlyOneEvent = false;       //! Prevents >1 events ran in 1 update call
    bool pushSensePeriodic = false;  //! Flag vs. periodic loop
    unsigned long pollStateTimeThreshold = POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD;
    unsigned long pushSenseTimeThreshold = POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD;
};

/**
 * 
 */
struct _polip_workflow_hooks {
    void (*pushStateSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*pushStateRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*pollStateRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*valueRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*pushSenseSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*pushSenseRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    void (*workflowErrorCb)(polip_device_t* dev, JsonDocument& doc, polip_workflow_source_t source) = NULL;
};

/**
 * 
 */
struct _polip_workflow_flags {
    bool stateChanged = false;       //! Externally state has changed
    bool senseChanged = false;       //! Externally sense has changed
    bool getValue = false;           //! Need refresh value
    polip_ret_code_t error = POLIP_OK; //! Last error encountered
};

/**
 * 
 */
struct _polip_workflow_timers {
    unsigned long pollTime = 0;      //! last poll event (ms)
    unsigned long senseTime = 0;     //! last sense event (ms)
};

/**
 * Object used within workflow routine for general update / behavior of polip
 * device.
 */
typedef struct _polip_workflow {
    /**
     * Pointer to device within this workflow
     */
    struct _polip_device *device = NULL;  
    /**
     * Inner table for parameters used during workflow
     * Defaults set as defined in struct
     */
    struct _polip_workflow_params params;
    /**
     * Inner table for setup / response hooks
     * Set to NULL if not used.
     */
    struct _polip_workflow_hooks hooks;
    /**
     * Inner table for event flags used during workflow
     * Normally set to false
     */
    struct _polip_workflow_flags flags;
    /**
     * Inner table for soft timers used during workflow
     * Must be initialized to current time before running periodic update!
     */
    struct _polip_workflow_timers timers;

} polip_workflow_t;

//==============================================================================
//  Public Function Prototypes
//==============================================================================

/**
 * Generalized workflow for polip device operation initializer in setup
 * @param wkObj workflow object with params, hooks, flags necessary to run
 * @param currentTime_ms time used to seed internal soft timers
 * @return polip_ret_code_t 
 */
polip_ret_code_t polip_workflow_initialize(polip_workflow_t* wkObj, unsigned long currentTime_ms);
/**
 * Generalized worflow for polip device operation in main event loop
 * @param wkObj workflow object with params, hooks, flags necessary to run
 * @param doc reference to JSON buffer (will clear/replace contents)
 * @param timestamp pointer to formatted tiemstamp string
 * @param currentTime_ms time generated from millis() for periodic update
 * @return polip_ret_code_t error enum any non-recoverable error condition during workflow; OK on success
 */
polip_ret_code_t polip_workflow_periodic_update(polip_workflow_t* wkObj, 
        JsonDocument& doc, const char* timestamp, unsigned long currentTime_ms);
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
polip_ret_code_t polip_getState(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
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
 * Pushes sensor state to the server
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_pushSensors(polip_device_t* dev, JsonDocument& doc, const char* timestamp);
/**
 * Gets message identifier value from server (used internally for synchronization)
 * @param client is reference to external 
 * @param dev pointer to device 
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
 * @param timestamp pointer to formated timestamp string
 * @return polip_ret_code_t error enum any non-recoverable error condition with server; OK on success
 */
polip_ret_code_t polip_getValue(polip_device_t* dev, JsonDocument& doc, const char* timestamp);

//==============================================================================

#endif /*POLIP_CLIENT_HPP*/