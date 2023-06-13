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

#include "polip-client.hpp"

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

    // Push RPC action to server
    if (wkObj->rpcWorkflow != NULL && wkObj->rpcWorkflow->flags.shouldPeriodicUpdate) {
        polip_ret_code_t polipCode = polip_rpc_workflow_periodic_update(
            wkObj->rpcWorkflow,  
            wkObj->device,
            doc,
            timestamp,
            wkObj->params.onlyOneEvent
        );

        // Process response
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            wkObj->flags.getValue = true;

        } else if (polipCode != POLIP_OK) {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_PUSH_STATE);
            }
        }

        eventCount++;
        yield();
    }

    // Push state to server
    if (wkObj->flags.stateChanged && !(wkObj->params.onlyOneEvent && wkObj->flags.getValue 
            && (eventCount >= 1))) {
        
        // Create state structure
        doc.clear();
        if (wkObj->hooks.pushStateSetupCb != NULL) {
            wkObj->hooks.pushStateSetupCb(wkObj->device, doc);
        }

        // Push state to server
        polip_ret_code_t polipCode = polip_pushState(
            wkObj->device, 
            doc, 
            timestamp
        );

        // Process response
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            wkObj->flags.getValue = true;

        } else if (polipCode == POLIP_OK) {
            wkObj->flags.stateChanged = false;
            wkObj->timers.pollTime = currentTime_ms; // Don't need to poll, current state just pushed
            if (wkObj->hooks.pushStateRespCb != NULL) {
                wkObj->hooks.pushStateRespCb(wkObj->device, doc);
            }

        } else {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_PUSH_STATE);
            }
        }

        eventCount++;
        yield();
    }

    // Poll server for state changes
    if (!wkObj->flags.stateChanged && ((currentTime_ms - wkObj->state.pollTimer) >= wkObj->params.pollStateTimeThreshold) 
            && !(wkObj->params.onlyOneEvent && wkObj->flags.getValue && (eventCount >= 1))) {
        
        // Poll state from server
        doc.clear();
        polip_ret_code_t polipCode = polip_getState(
            wkObj->device,
            doc, 
            timestamp,
            wkObj->params.pollState,
            wkObj->params.pollManufacturer,
            wkObj->params.pollRPC
        );

        // Process response
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            wkObj->flags.getValue = true;
            
        } else if (polipCode == POLIP_OK) {
            wkObj->state.pollTimer = currentTime_ms;
            
            if (wkObj->hooks.pollStateRespCb != NULL) {
                wkObj->hooks.pollStateRespCb(wkObj->device, doc);
            }

            if (wkObj->rpcWorkflow != NULL) {
                polip_rpc_workflow_poll_event(wkObj->rpcWorkflow, wkObj->device, doc);
            }

        } else {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_POLL_STATE);
            }
        }

        eventCount++;
        yield();
    }

    // Push sensor state to server
    if ((wkObj->flags.senseChanged || (wkObj->params.pushSensePeriodic 
            && (currentTime_ms - wkObj->state.senseTimer) >= wkObj->params.pushSenseTimeThreshold)) 
            && !(wkObj->params.onlyOneEvent && wkObj->flags.getValue && (eventCount >= 1))) {
        
        // Create state structure
        doc.clear();
        if (wkObj->hooks.pushSenseSetupCb != NULL) {
            wkObj->hooks.pushSenseSetupCb(wkObj->device, doc);
        }

        // Push sense to server
        polip_ret_code_t polipCode = polip_pushSensors(
            wkObj->device,
            doc, 
            timestamp
        );

        // Process response
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            wkObj->flags.getValue = true;
            
        } else if (polipCode == POLIP_OK) {
            wkObj->state.senseTimer = currentTime_ms;
            if (wkObj->hooks.pushSenseRespCb != NULL) {
                wkObj->hooks.pushSenseRespCb(wkObj->device, doc);
            }

        } else {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_PUSH_SENSE);
            }
        }

        eventCount++;
        yield();
    }

    // Attempt to get sync value from server
    if (wkObj->flags.getValue && !(wkObj->params.onlyOneEvent && (eventCount >= 1))) {
        wkObj->flags.getValue = false;
        doc.clear();
        
        polip_ret_code_t polipCode = polip_getValue(
            wkObj->device,
            doc, 
            timestamp
        );
        
        if (polipCode != POLIP_OK) {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_GET_VALUE);
            }
        } else {
            if (wkObj->hooks.valueRespCb != NULL) {
                wkObj->hooks.valueRespCb(wkObj->device, doc);
            }
        }

        eventCount++;
        yield();
    }

    return retStatus;
}