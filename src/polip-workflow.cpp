/**
 * @file polip-workflow.cpp
 * @author Curt Henrichs
 * @brief Polip Workflow 
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib wrapper that provides a nice standardized workflow with 
 * extensive, customizable callbacks hooks.
 */

//==============================================================================
//  Libraries
//==============================================================================

#include <Arduino.h>

#include "./polip-workflow.hpp"

//==============================================================================
//  Preprocessor Macro Declaration
//==============================================================================

#define WORKFLOW_EVENT_TEMPLATE(_condition_, _setup_, _req_, _res_,                 \
        wkObjPtr, doc, eventCount, valueRetry, source, retStatus) {                 \
    if ((_condition_) && !(wkObj->params.onlyOneEvent                               \
                      && (wkObj->flags.getValue && !valueRetry)                     \
                      && (eventCount >= 1))) {                                      \
        doc.clear();                                                                \
        _setup_;                                                                    \
        polip_ret_code_t polipCode = _req_;                                         \
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH && valueRetry) {                \
            (wkObjPtr)->flags.getValue = true;                                      \
        } else if (polipCode == POLIP_OK) {                                         \
            _req_;                                                                  \
        } else {                                                                    \
            (wkObjPtr)->flags.error = polipCode;                                    \
            retStatus = POLIP_ERROR_WORKFLOW;                                       \
            if ((wkObjPtr)->hooks.workflowErrorCb != NULL) {                        \
                (wkObjPtr)->hooks.workflowErrorCb((wkObjPtr)->device, doc, source); \
            }                                                                       \
        }                                                                           \
        eventCount++;                                                               \
        yield();                                                                    \
    }                                                                               \
}

//==============================================================================
//  Public Function Implementation
//==============================================================================

polip_ret_code_t polip_workflow_initialize(polip_workflow_t* wkObj, unsigned long currentTime_ms) {
    wkObj->flags.stateChanged = false;
    wkObj->flags.senseChanged = false;
    wkObj->flags.getValue = false;
    wkObj->flags.error = POLIP_OK;
    
    wkObj->state.pollTimer = currentTime_ms;
    wkObj->state.senseTimer = currentTime_ms;

    polip_ret_code_t status = POLIP_OK;
    if (wkObj->rpcWorkflow != NULL) {
        status = polip_rpc_workflow_initialize(wkObj->rpcWorkflow);

        // If not already bound, then link general workflow error handler with RPC workflow error handler
        if (wkObj->rpcWorkflow->hooks.workflowErrorCb == NULL) {
            wkObj->rpcWorkflow->hooks.workflowErrorCb = wkObj->hooks.workflowErrorCb;
        }
    }
    
    return status;
}

polip_ret_code_t polip_workflow_teardown(polip_workflow_t* wkObj) {
    polip_ret_code_t status = POLIP_OK;
    if (wkObj->rpcWorkflow != NULL) {
        status = polip_rpc_workflow_teardown(wkObj->rpcWorkflow);
    }
    return status;
}

polip_ret_code_t polip_workflow_periodic_update(polip_workflow_t* wkObj, 
        JsonDocument& doc, const char* timestamp, unsigned long currentTime_ms) {
    polip_ret_code_t retStatus = POLIP_OK;
    unsigned int eventCount = 0;

    // Serial.println(currentTime_ms);

    WORKFLOW_EVENT_TEMPLATE(
        (
            !wkObj->flags.stateChanged && ((currentTime_ms - wkObj->state.pollTimer) >= wkObj->params.pollStateTimeThreshold) 
        ),
        {
            Serial.print("Setup @ ");
            Serial.println(currentTime_ms);
        },
        (
            POLIP_OK
        ),
        {
            Serial.println("Handler");
        },
        wkObj,doc, eventCount, true, POLIP_WORKFLOW_PUSH_STATE, retStatus
    );

    // // Push RPC action to server
    // WORKFLOW_EVENT_TEMPLATE(
    //     (
    //         wkObj->rpcWorkflow != NULL && wkObj->rpcWorkflow->flags.shouldPeriodicUpdate
    //     ),
    //     {},
    //     (
    //         polip_rpc_workflow_periodic_update(
    //             wkObj->rpcWorkflow,  
    //             wkObj->device,
    //             doc,
    //             timestamp,
    //             wkObj->params.onlyOneEvent
    //         )
    //     ),
    //     {}, 
    //     wkObj,doc, eventCount, true, POLIP_WORKFLOW_PUSH_STATE, retStatus
    // );

    // // Push state to server
    // WORKFLOW_EVENT_TEMPLATE(
    //     (
    //         wkObj->flags.stateChanged 
    //     ), {
    //         if (wkObj->hooks.pushStateSetupCb != NULL) {
    //             wkObj->hooks.pushStateSetupCb(wkObj->device, doc);
    //         }
    //     }, (
    //         polip_pushState(
    //             wkObj->device, 
    //             doc, 
    //             timestamp
    //         )
    //     ), {
    //         wkObj->flags.stateChanged = false;
    //         wkObj->state.pollTimer = currentTime_ms; // Don't need to poll, current state just pushed
    //         if (wkObj->hooks.pushStateRespCb != NULL) {
    //             wkObj->hooks.pushStateRespCb(wkObj->device, doc);
    //         }
    //     }, wkObj,doc, eventCount, true, POLIP_WORKFLOW_PUSH_STATE, retStatus
    // );

    // // Poll server for state changes
    // WORKFLOW_EVENT_TEMPLATE(
    //     (
    //         !wkObj->flags.stateChanged && ((currentTime_ms - wkObj->state.pollTimer) >= wkObj->params.pollStateTimeThreshold) 
    //     ), {}, (
    //         polip_getState(
    //             wkObj->device,
    //             doc, 
    //             timestamp,
    //             wkObj->params.pollState,
    //             wkObj->params.pollManufacturer,
    //             (wkObj->rpcWorkflow != NULL)
    //         )
    //     ), {
    //         wkObj->state.pollTimer = currentTime_ms;
            
    //         if (wkObj->hooks.pollStateRespCb != NULL) {
    //             wkObj->hooks.pollStateRespCb(wkObj->device, doc);
    //         }

    //         // if (wkObj->rpcWorkflow != NULL) {
    //         //     polip_rpc_workflow_poll_event(
    //         //         wkObj->rpcWorkflow, 
    //         //         wkObj->device, 
    //         //         doc, 
    //         //         timestamp
    //         //     );
    //         // }
    //     }, wkObj,doc, eventCount, true, POLIP_WORKFLOW_POLL_STATE, retStatus
    // );

    // // Push sensor state to server
    // WORKFLOW_EVENT_TEMPLATE(
    //     (wkObj->flags.senseChanged || (wkObj->params.pushSensePeriodic &&
    //         (currentTime_ms - wkObj->state.senseTimer) >= wkObj->params.pushSenseTimeThreshold)
    //     ), {
    //         if (wkObj->hooks.pushSenseSetupCb != NULL) {
    //             wkObj->hooks.pushSenseSetupCb(wkObj->device, doc);
    //         }
    //     }, (
    //         polip_pushSensors(
    //             wkObj->device,
    //             doc, 
    //             timestamp
    //         )
    //     ), {
    //         wkObj->state.senseTimer = currentTime_ms;
    //         if (wkObj->hooks.pushSenseRespCb != NULL) {
    //             wkObj->hooks.pushSenseRespCb(wkObj->device, doc);
    //         }
    //     },
    //     wkObj,doc, eventCount, true, POLIP_WORKFLOW_PUSH_SENSE, retStatus
    // );

    // // Attempt to get sync value from server
    // WORKFLOW_EVENT_TEMPLATE(
    //     (
    //         wkObj->flags.getValue
    //     ), {
    //         wkObj->flags.getValue = false;
    //     }, (
    //         polip_getValue(
    //             wkObj->device,
    //             doc, 
    //             timestamp
    //         )
    //     ), {
    //         if (wkObj->hooks.valueRespCb != NULL) {
    //             wkObj->hooks.valueRespCb(wkObj->device, doc);
    //         }
    //     }, wkObj,doc, eventCount, false, POLIP_WORKFLOW_GET_VALUE, retStatus
    // );

    return retStatus;
}