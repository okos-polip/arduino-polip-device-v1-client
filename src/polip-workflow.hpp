/**
 * @file polip-workflow.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

#ifndef POLIP_WORKFLOW_HPP
#define POLIP_WORKFLOW_HPP

//==============================================================================
//  Libraries
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <ArduinoJson.h>

#include "./polip-core.hpp"
#include "./polip-device.hpp"
#include "./polip-rpc-workflow.hpp"

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! Periodic poll of server device state
#ifndef POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD
#define POLIP_DEFAULT_POLL_STATE_TIME_THRESHOLD     (1000L)
#endif

//! Periodic push of device sensors
#ifndef POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD
#define POLIP_DEFAULT_PUSH_SENSE_TIME_THRESHOLD     (1000L)
#endif

//==============================================================================
//  Preprocessor Macros
//==============================================================================

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

#define POLIP_WORKFLOW_RPC_CHANGED(workflowPtr) {                               \
    POLIP_RPC_WORKFLOW_RPC_CHANGED((workflowPtr)->rpcWorkflow);                 \
}

//==============================================================================
//  Public Data Structure Declaration
//==============================================================================

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
        void (*workflowErrorCb)(polip_device_t* dev, JsonDocument& doc, polip_workflow_source_t source, polip_ret_code_t error) = NULL;
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

//==============================================================================

#endif //POLIP_WORKFLOW_HPP