/**
 * @file polip-rpc-workflow.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

#ifndef POLIP_RPC_WORKFLOW_HPP
#define POLIP_RPC_WORKFLOW_HPP

//==============================================================================
//  Libraries
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <ArduinoJson.h>

#include "./polip-core.hpp"
#include "./polip-device.hpp"

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! RPC status options
//    Can push all but canceled (since that happens server side) - use reject if need to drop
//    On poll expect pending, acknowledged
#define POLIP_RPC_STATUS_PENDING_STR                    "pending"
#define POLIP_RPC_STATUS_SUCCESS_STR                    "success"
#define POLIP_RPC_STATUS_FAILURE_STR                    "failure"
#define POLIP_RPC_STATUS_REJECTED_STR                   "rejected"
#define POLIP_RPC_STATUS_ACKNOWLEDGED_STR               "acknowledged"
#define POLIP_RPC_STATUS_CANCELED_STR                   "canceled"

//! Buffer size needed to store unique id stirng for RPCs
#ifndef POLIP_RPC_UUID_BUFFER_SIZE
#define POLIP_RPC_UUID_BUFFER_SIZE                  (50)
#endif

//! Buffer size needed to store unqiue type string for RPCs
#ifndef POLIP_RPC_TYPE_BUFFER_SIZE
#define POLIP_RPC_TYPE_BUFFER_SIZE                  (50)
#endif

//==============================================================================
//  Preprocessor Macros
//==============================================================================

#define POLIP_RPC_WORKFLOW_RPC_CHANGED(rpcWorkflowPtr) {                        \
    (rpcWorkflowPtr)->flags.shouldPeriodicUpdate = true;                        \
}

#define POLIP_RPC_WORKFLOW_SHOULD_ACCEPT_NEW_RPCS(rpcWorkflowPtr, state) {      \
    (rpcWorkflowPtr)->state.allowingNewRPCs = (state);                          \
}

#define POLIP_RPC_WORKFLOW_ASSIGN_CORE_HOOKS(rpcWorkflowPtr,                    \
        acceptRPC_fnt, cancelRPC_fnt) {                                         \
    (rpcWorkflowPtr)->hooks.acceptRPC = acceptRPC_fnt;                          \
    (rpcWorkflowPtr)->hooks.cancelRPC = cancelRPC_fnt;                          \
}

#define POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, status) {      \
    (rpcPtr)->_nextStatus = status;                                             \
    (rpcWorkflowPtr)->flags.shouldPeriodicUpdate = true;                        \
}

#define POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWorkflowPtr, rpcPtr) (                             \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_REJECTED)     \
)

#define POLIP_RPC_WORKFLOW_RPC_SUCCEEDED(rpcWorkflowPtr, rpcPtr) (                          \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_SUCCESS)      \
)

#define POLIP_RPC_WORKFLOW_RPC_FAILED(rpcWorkflowPtr, rpcPtr) (                             \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_FAILURE)      \
)

#define POLIP_RPC_WORKFLOW_ACKNOWLEDGE_RPC(rpcWorkflowPtr, rpcPtr) (                        \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_ACKNOWLEDGED) \
)

#define POLIP_RPC_WORKFLOW_CLIENT_SIDE_CANCEL_RPC(rpcWorkflowPtr, rpcPtr) (                 \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_CANCELED)     \
)

#define POLIP_RPC_WORKFLOW_CLIENT_SIDE_PENDING_RPC(rpcWorkflowPtr, rpcPtr) (                \
    POLIP_RPC_WORKFLOW_UPDATE_STATUS(rpcWorkflowPtr, rpcPtr, POLIP_RPC_STATUS_PENDING)      \
)

//==============================================================================
//  Enumerated Constants
//==============================================================================

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
 * RPC Entry representation (for workflow)
 */
typedef struct _polip_rpc {

    /**
     * Status with specific semantics to walk through state-table
     */
    enum _polip_rpc_status status = _RPC_STATUS_UNKNOWN;

    /**
     * UUID for specific RPC
     */
    char uuid[POLIP_RPC_UUID_BUFFER_SIZE];

    /**
     * Unique type defined by server : should match against user-firmware expectation
     */
    char type[POLIP_RPC_TYPE_BUFFER_SIZE];

    /**
     * On accept, user-firmware can link this to some useful value or leave NULL
     */
    void* userContext = NULL;

    /**
     * Next status to update server to
     */
    enum _polip_rpc_status _nextStatus = _RPC_STATUS_UNKNOWN;

    /**
     * Pointer to next RPC in linked list
     */
    struct _polip_rpc *_nextPtr = NULL;

    /**
     * Ctrl bit, indicates checked against server list during Poll event
     */
    bool _checked = false;

} polip_rpc_t;

/**
 * Object used within workflow routine for RPCs
 */
typedef struct _polip_rpc_workflow {

    /**
     * Pointer to array of RPCs
     */
    struct _polip_rpc *_allocatedRPCs = NULL;

    /**
     * Configuration parameters for workflow algorithm
     */
    struct _polip_rpc_workflow_params {
        unsigned int maxActiveRPCs = 1;  //! Number of RPCs allowed
        bool pushAdditionalNotification = false; //! In addition to pushing RPC status, also send message on notification route
        bool onHeap = true; //! Will allocate buffer on initialization
    } params;

    /**
     * Callback functions defined by user
     */
    struct _polip_rpc_workflow_hooks {
        void (*newRPC)(polip_device_t* dev, polip_rpc_t* rpc, JsonObject& parameters) = NULL;
        void (*freeRPC)(polip_device_t* dev, polip_rpc_t* rpc) = NULL;
        bool (*cancelRPC)(polip_device_t* dev, polip_rpc_t* rpc) = NULL;
        bool (*acceptRPC)(polip_device_t* dev, polip_rpc_t* rpc, JsonObject& parameters) = NULL;
        bool (*reacceptRPC)(polip_device_t* dev, polip_rpc_t* rpc, JsonObject& parameters) = NULL;
        void (*pushRPCSetup)(polip_device_t* dev, polip_rpc_t* rpc, JsonDocument& doc) = NULL;
        void (*pushRPCResponse)(polip_device_t* dev, polip_rpc_t* rpc, JsonDocument& doc) = NULL;
        void (*pushNotifactionSetup)(polip_device_t* dev, polip_rpc_t* rpc, JsonDocument& doc) = NULL;
        void (*pushNotifactionResponse)(polip_device_t* dev, polip_rpc_t* rpc, JsonDocument& doc) = NULL;
        bool (*shouldDeleteExtraRPC)(polip_device_t* dev, polip_rpc_t* rpc) = NULL;
        void (*workflowErrorCb)(polip_device_t* dev, JsonDocument& doc, polip_workflow_source_t source) = NULL;
    } hooks;

    /**
     * Workflow active flags
     */
    struct _polip_rpc_workflow_flags {
        bool shouldPeriodicUpdate = false;  //! External signal used to trigger workflow
    } flags;

    /**
     * Workflow internal state
     */
    struct _polip_rpc_workflow_state {
        bool allowingNewRPCs = true;
        unsigned int numActiveRPCs = 0;  //! Current number of RPCs being processed
        struct _polip_rpc *_activePtr = NULL;
        struct _polip_rpc *_freePtr = NULL;
        bool _masterCheckedBit = false;
    } state;

} polip_rpc_workflow_t;

//==============================================================================
//  Public Function Prototypes
//==============================================================================

polip_ret_code_t polip_rpc_workflow_initialize(polip_rpc_workflow_t* rpcWkObj);

polip_ret_code_t polip_rpc_workflow_teardown(polip_rpc_workflow_t* rpcWkObj);

polip_ret_code_t polip_rpc_workflow_periodic_update(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool singleEvent);

polip_ret_code_t polip_rpc_workflow_poll_event(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp);

const char* polip_rpc_status_enum2str(polip_rpc_status_t status);

polip_rpc_status_t polip_rpc_status_str2enum(const char* str);

polip_rpc_t* polip_rpc_workflow_new_rpc(polip_rpc_workflow_t* rpcWkObj, polip_rpc_status_t status, 
        const char* uuid, const char* type, JsonObject& paramObj, polip_device_t* dev);

bool polip_rpc_workflow_free_rpc(polip_rpc_workflow_t* rpcWkObj, polip_rpc_t* rpc);

polip_rpc_t* polip_rpc_workflow_get_rpc_by_uuid(polip_rpc_workflow_t* rpcWkObj, const char* uuid);

polip_ret_code_t polip_rpc_workflow_push_status(polip_rpc_workflow_t* rpcWkObj, polip_rpc_t* rpc, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp);

//==============================================================================

#endif //POLIP_RPC_WORKFLOW_HPP