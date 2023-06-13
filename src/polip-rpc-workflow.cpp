/**
 * @file polip-rpc-workflow.cpp
 * @author Curt Henrichs
 * @brief Polip Workflow 
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib wrapper that provides a nice standardized RPC workflow with 
 * extensive, customizable callbacks hooks. Used as a submodule of the overall
 * workflow.
 */

//==============================================================================
//  Libraries
//==============================================================================

#include "polip-client.hpp"

//==============================================================================
//  Public Function Implementation
//==============================================================================

polip_ret_code_t polip_rpc_workflow_initialize(polip_rpc_workflow_t* rpcWkObj) {
    if (rpcWkObj->activeRPCs != NULL) {
        if (rpcWkObj->params.onHeap) {
            return POLIP_ERROR_WORKFLOW;
        } else {
            return POLIP_OK;
        }
    }

    rpcWkObj->activeRPCs = new polip_rpc_t[rpcWkObj->params.maxActivedRPCs];
    if (rpcWkObj->activeRPCs == NULL) {
        return POLIP_ERROR_WORKFLOW;
    } else {
        return POLIP_OK;
    }
}

polip_ret_code_t polip_rpc_workflow_teardown(polip_rpc_workflow_t* rpcWkObj) {
    if (!rpcWkObj->params.onHeap) {
        return POLIP_OK;
    } else if (rpcWkObj->activeRPCs == NULL) {
        return POLIP_ERROR_WORKFLOW;
    }

    delete[] rpcWkObj->activeRPCs;
    rpcWkObj->activeRPCs = NULL;
    return POLIP_OK;
}

polip_ret_code_t polip_rpc_workflow_periodic_update(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool single_msg) {
    rpcWkObj->flags.shouldPeriodicUpdate = false;
    unsigned int eventCount = 0;
    
    // if (wkObj->flags.rpcChanged && !(wkObj->params.onlyOneEvent && wkObj->flags.getValue 
    //         && (eventCount >= 1))) {
        
    //     // Create RPC structure
    //     doc.clear();
    //     if (wkObj->hooks.pushRPCSetupCb != NULL) {
    //         wkObj->hooks.pushRPCSetupCb(wkObj->device, doc);
    //     }

    //     // Push RPC to server
    //     polip_ret_code_t polipCode = polip_pushRPC(
    //         wkObj->device,
    //         doc,
    //         timestamp
    //     );

    //     // Process response
    //     if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
    //         wkObj->flags.getValue = true;

    //     } else if (polipCode == POLIP_OK) {
    //         wkObj->flags.rpcChanged = false;
    //         if (wkObj->hooks.pushRPCRespCb != NULL) {
    //             wkObj->hooks.pushRPCRespCb(wkObj->device, doc);
    //         }

    //     } else {
    //         wkObj->flags.error = polipCode;
    //         retStatus = POLIP_ERROR_WORKFLOW;
    //         if (wkObj->hooks.workflowErrorCb != NULL) {
    //             wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_PUSH_RPC);
    //         }
    //     }

    //     eventCount++;
    //     yield();
    // }
}

polip_ret_code_t polip_rpc_workflow_poll_event(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool single_msg) {
    //TODO need to handle the add / remove logic from list with a better mechanism

    JsonArray array = doc["rpc"].as<JsonArray>();
    for(JsonObject rpcObj : array) {
        String uuid = rpcObj["uuid"]; 
        String type = rpcObj["type"];
        polip_rpc_status_t status = polip_rpc_status_str2enum(((String)rpcObj["status"]).c_str());
        JsonObject paramObj = rpcObj["parameters"];

        // check if uuid in list
        bool found = false;
        for (int i=0; i<rpcWkObj->state.numActiveRPCs; i++) {
            polip_rpc_t* entry = &rpcWkObj->activeRPCs[i];

            if (uuid == entry->uuid) {
                found = true;

                if (status == POLIP_RPC_STATUS_CANCELED) {
                    // if (rpcWkObj->hooks.cancelRPC(dev, entry)) {
                    //     // TODO push Acknowledge cancel to periodic
                    // } else {
                    //     // TODO push Reject cancel to periodic
                    // }
                } else if (status == POLIP_RPC_STATUS_PENDING) {
                    //TODO callback to acknowledge (somehow it got set back to pending state?)
                }
                // else should be acknowledge state -  anything else is server error

                // Found match, can end loop
                //  If canceled, need to retry at start of list for next RPC, will wait for next poll
                //  If pending, need to push ack (flag is set)
                //  If acknowledged, already in good state nothing to do
                break; 
            }
        }

        // Can skip ahead if this RPC had already been handled
        if (found) {
            continue;
        }

        // check if can accept another RPC
        if (rpcWkObj->flags.numActiveRPCs < rpcWkObj->params.maxActivedRPCs) {
            //TODO add RPC to list, move RPC into acknowledged state, rejected state, canceled state, or keep pending state
        } else {
            // Can't add this RPC to list so continue on to next
            continue;
        }
    }
}

const char* polip_rpc_status_enum2str(polip_rpc_status_t status) {
    switch (status) {
        case POLIP_RPC_STATUS_PENDING:
            return POLIP_RPC_STATUS_PENDING_STR;                   
        case POLIP_RPC_STATUS_SUCCESS:
            return POLIP_RPC_STATUS_SUCCESS_STR;                   
        case POLIP_RPC_STATUS_FAILURE:
            return POLIP_RPC_STATUS_FAILURE_STR;                   
        case POLIP_RPC_STATUS_REJECTED:
            return POLIP_RPC_STATUS_REJECTED_STR;                 
        case POLIP_RPC_STATUS_ACKNOWLEDGED:
            return POLIP_RPC_STATUS_ACKNOWLEDGED_STR;             
        case POLIP_RPC_STATUS_CANCELED:
            return POLIP_RPC_STATUS_CANCELED_STR;
        default:
            return NULL;
    }
}

polip_rpc_status_t polip_rpc_status_str2enum(const char* str) {
    if (strcmp(str, POLIP_RPC_STATUS_PENDING_STR) == 0) {
        return POLIP_RPC_STATUS_PENDING;
    } else if (strcmp(str, POLIP_RPC_STATUS_SUCCESS_STR) == 0) {
        return POLIP_RPC_STATUS_SUCCESS;
    } else if (strcmp(str, POLIP_RPC_STATUS_FAILURE_STR) == 0) {
        return POLIP_RPC_STATUS_FAILURE;
    } else if (strcmp(str, POLIP_RPC_STATUS_REJECTED_STR) == 0) {
        return POLIP_RPC_STATUS_REJECTED;
    } else if (strcmp(str, POLIP_RPC_STATUS_ACKNOWLEDGED_STR) == 0) {
        return POLIP_RPC_STATUS_ACKNOWLEDGED;
    } else if (strcmp(str, POLIP_RPC_STATUS_CANCELED_STR) == 0) {
        return POLIP_RPC_STATUS_CANCELED;
    } else {
        return _RPC_STATUS_UNKNOWN;
    }
}