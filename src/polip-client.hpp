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

//! Allows POLIP lib to print out debug information on serial bus
#ifndef POLIP_VERBOSE_DEBUG
#define POLIP_VERBOSE_DEBUG                         (false)
#endif

//! Minimum buffer size, larger if state or sense is substatial
#ifndef POLIP_ARBITRARY_MSG_BUFFER_SIZE
#define POLIP_ARBITRARY_MSG_BUFFER_SIZE             (512)
#endif

#ifndef POLIP_QUERY_URI_BUFFER_SIZE
#define POLIP_QUERY_URI_BUFFER_SIZE                 (128)
#endif

//! Periodic poll of server device state
#ifndef POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD
#define POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD     (1000L)
#endif

//! Periodic push of device sensors
#ifndef POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD
#define POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD     (1000L)
#endif

//! RPC status options
//    Can push all but canceled (since that happens server side) - use reject if need to drop
//    On poll expect pending, acknowledged
#define POLIP_RPC_STATUS_PENDING_STR                    "pending"
#define POLIP_RPC_STATUS_SUCCESS_STR                    "success"
#define POLIP_RPC_STATUS_FAILURE_STR                    "failure"
#define POLIP_RPC_STATUS_REJECTED_STR                   "rejected"
#define POLIP_RPC_STATUS_ACKNOWLEDGED_STR               "acknowledged"
#define POLIP_RPC_STATUS_CANCELED_STR                   "canceled"

//==============================================================================
//  Preprocessor Macros
//==============================================================================

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

#define POLIP_WORKFLOW_RPC_CHANGED(workflowPtr) {                              \
    (workflowPtr)->flags.rpcChanged = true;                                    \
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

#define polip_pushNotification(dev, doc, timestamp) (                           \
    polip_pushError(                                                            \
        dev,                                                                    \
        doc,                                                                    \
        timestamp                                                               \
    )                                                                           \
)

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
    POLIP_WORKFLOW_PUSH_STATE,
    POLIP_WORKFLOW_POLL_STATE,
    POLIP_WORKFLOW_GET_VALUE,
    POLIP_WORKFLOW_PUSH_SENSE,
    POLIP_WORKFLOW_PUSH_RPC
} polip_workflow_source_t;

/**
 * RPC state
 */
typedef enum _polip_rpc_status {
    POLIP_RPC_STATUS_PENDING,                    
    POLIP_RPC_STATUS_SUCCESS,                    
    POLIP_RPC_STATUS_FAILURE,                    
    POLIP_RPC_STATUS_REJECTED,                   
    POLIP_RPC_STATUS_ACKNOWLEDGED,               
    POLIP_RPC_STATUS_CANCELED,
    _RPC_STATUS_UNKNOWN                  
} polip_rpc_status_t;

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
typedef struct _polip_rpc {
    enum _polip_rpc_status status = _RPC_STATUS_UNKNOWN;
    char uuid[50];
    char type[50];
    void* userContext = NULL;
} polip_rpc_t;

/**
 * 
 */
typedef struct _polip_rpc_workflow {

    /**
     * Pointer to array of RPCs
     */
    struct _polip_rpc *activeRPCs = NULL;

    /**
     * 
     */
    struct _polip_rpc_workflow_params {
        unsigned int maxActivedRPCs = 1;  //! Number of RPCs allowed
        bool pushAdditionalNotification = false; //! In addition to pushing RPC status, also send message on notification route
        bool onHeap = true; //! Will allocate buffer on initialization
    } params;

    /**
     * Callback functions defined by user
     */
    struct _polip_rpc_workflow_hooks {
        void (*pollRPCRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushRPCAckSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushRPCAckRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushRPCFinishSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushRPCFinishRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
    } hooks;

    struct _polip_rpc_workflow_flags {
        bool shouldPeriodicUpdate = false;
    } flags;

    /**
     * Workflow internal state
     */
    struct _polip_rpc_workflow_state {
        unsigned int numActiveRPCs = 0;  //! Current number of RPCs being processed
    } state;

} polip_rpc_workflow_t;

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
     * Pointer to RPC workflow
     */
    struct _polip_rpc_workflow * rpcWorkflow = NULL;
    
    /**
     * Inner table for parameters used during workflow
     * Defaults set as defined in struct
     */
    struct _polip_workflow_params {
        bool onlyOneEvent = false;       //! Prevents >1 events ran in 1 update call
        bool pushSensePeriodic = false;  //! Flag vs. periodic loop
        bool pollState = true;           //! Allows override of check state during poll
        bool pollManufacturer = false;   //! Checks manufacturer defined data while polling
        unsigned long pollStateTimeThreshold = POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD;
        unsigned long pushSenseTimeThreshold = POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD;
    } params;
    
    /**
     * Inner table for setup / response hooks
     * Set to NULL if not used.
     */
    struct _polip_workflow_hooks {
        void (*pushStateSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushStateRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pollStateRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*valueRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushSenseSetupCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*pushSenseRespCb)(polip_device_t* dev, JsonDocument& doc) = NULL;
        void (*workflowErrorCb)(polip_device_t* dev, JsonDocument& doc, polip_workflow_source_t source) = NULL;
    } hooks;
    
    /**
     * Inner table for event flags used during workflow
     * Normally set to false
     */
    struct _polip_workflow_flags {
        bool stateChanged = false;       //! Externally state has changed
        bool senseChanged = false;       //! Externally sense has changed
        bool getValue = false;           //! Need refresh value
        polip_ret_code_t error = POLIP_OK; //! Last error encountered
    } flags;
    
    /**
     * Inner table for state used during workflow
     * Must be initialized to current time before running periodic update!
     */
    struct _polip_workflow_state {
        unsigned long pollTimer = 0;      //! last poll event (ms)
        unsigned long senseTimer = 0;     //! last sense event (ms)
    } state;

} polip_workflow_t;

//==============================================================================
//  Public Function Prototypes
//==============================================================================

//-----------------------------------------------------------------------------
//  Workflow Functions
//-----------------------------------------------------------------------------

/**
 * @brief Generalized workflow for polip device operation initializer 
 * to call during setup
 * 
 * @param wkObj workflow object with params, hooks, flags necessary to run
 * @param currentTime_ms time used to seed internal soft timers
 * @return polip_ret_code_t error enum any non-recoverable error condition during workflow; OK on success
 */
polip_ret_code_t polip_workflow_initialize(polip_workflow_t* wkObj, unsigned long currentTime_ms);
/**
 * @brief Generalized workflow for polip device operation destructor, call only
 * if destroying context (advanced use only!)
 * 
 * @param wkObj workflow object with params, hooks, flags necessary to run
 * @return polip_ret_code_t error enum any non-recoverable error condition during workflow; OK on success
 */
polip_ret_code_t polip_workflow_teardown(polip_workflow_t* wkObj);
/**
 * @brief Generalized worflow for polip device operation in main event loop
 * 
 * @param wkObj workflow object with params, hooks, flags necessary to run
 * @param doc reference to JSON buffer (will clear/replace contents)
 * @param timestamp pointer to formatted tiemstamp string
 * @param currentTime_ms time generated from millis() for periodic update
 * @return polip_ret_code_t error enum any non-recoverable error condition during workflow; OK on success
 */
polip_ret_code_t polip_workflow_periodic_update(polip_workflow_t* wkObj, 
        JsonDocument& doc, const char* timestamp, unsigned long currentTime_ms);

//-----------------------------------------------------------------------------
//  RPC Workflow Functions
//-----------------------------------------------------------------------------

polip_ret_code_t polip_rpc_workflow_initialize(polip_rpc_workflow_t* rpcWkObj);

polip_ret_code_t polip_rpc_workflow_teardown(polip_rpc_workflow_t* rpcWkObj);

polip_ret_code_t polip_rpc_workflow_periodic_update(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool single_msg);

polip_ret_code_t polip_rpc_workflow_poll_event(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool single_msg);

const char* polip_rpc_status_enum2str(polip_rpc_status_t status);

polip_rpc_status_t polip_rpc_status_str2enum(const char* str);

//-----------------------------------------------------------------------------
//  Device Functions
//-----------------------------------------------------------------------------

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
 * @param doc reference to JSON buffer (will clear/replace contents) - should initially contain sense field
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

#endif /*POLIP_CLIENT_HPP*/