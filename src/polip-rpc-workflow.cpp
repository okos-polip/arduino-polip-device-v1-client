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

#include "./polip-rpc-workflow.hpp"

//==============================================================================
//  Public Function Implementation
//==============================================================================

polip_ret_code_t polip_rpc_workflow_initialize(polip_rpc_workflow_t* rpcWkObj) {
    if (rpcWkObj->activeRPCs != NULL && rpcWkObj->params.onHeap) {
        return POLIP_ERROR_WORKFLOW;
    }

    rpcWkObj->activeRPCs = new polip_rpc_t[rpcWkObj->params.maxActivedRPCs];
    if (rpcWkObj->activeRPCs == NULL) {
        return POLIP_ERROR_WORKFLOW;
    }
    
    // Setup rpc list manager
    rpcWkObj->state._activePtr = NULL;
    rpcWkObj->state._freePtr = rpcWkObj->_allocatedRPCs;
    rpcWkObj->state.numActiveRPCs = 0;

    // Initialize each RPC with pointer in list
    for (int i=0; i<rpcWkObj->params.maxActivedRPCs; i++) {
        rpcWkObj->_allocatedRPCs[i]._nextPtr = (i+1 == rpcWkObj->params.maxActivedRPCs) ? NULL : &rpcWkObj->_allocatedRPCs[i+1];
    }
    
    return POLIP_OK;
}

polip_ret_code_t polip_rpc_workflow_teardown(polip_rpc_workflow_t* rpcWkObj) {
    if (rpcWkObj->params.onHeap && rpcWkObj->activeRPCs != NULL) {
        delete[] rpcWkObj->_allocatedRPCs;
        rpcWkObj->activeRPCs = NULL;
    }

    rpcWkObj->state._activePtr = NULL;
    rpcWkObj->state._freePtr = NULL;
    rpcWkObj->state.numActiveRPCs = 0;

    return POLIP_OK;
}

polip_ret_code_t polip_rpc_workflow_periodic_update(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp, bool singleEvent) {
    rpcWkObj->flags.shouldPeriodicUpdate = false;
    unsigned int eventCount = 0;
    polip_ret_code_t polipCode = POLIP_OK;
    bool entryDeleted = false;
    polip_rpc_t *nextEntry = NULL, *entry = rpcWkObj->state._activePtr;

    while (entry != NULL && !(singleEvent && eventCount >= 1 && polipCode == POLIP_OK)) {
        nextEntry = entry._nextPtr;
        entryDeleted = false;

        if (entry->_checked != rpcWkObj->state._masterCheckedBit && !entryDeleted) {
            // RPC entry was not in last server poll list

            if (rpcWkObj->hooks.shouldDeleteExtraRPC != NULL) {
                if (rpcWkObj->hooks.shouldDeleteExtraRPC(dev, entry)) {
                    // Free RPC, will not throw error
                    polip_rpc_workflow_free_rpc(rpcWkObj, entry);
                    entryDeleted = true;
                } else {
                    // Keep arround
                    entry->_checked = rpcWkObj->state._masterCheckedBit;
                }
            } else {
                // Default behavior is to free, will throw error
                
                polip_rpc_workflow_free_rpc(rpcWkObj, entry);
                polipCode = POLIP_ERROR_WORKFLOW;
                entryDeleted = true;
            }

            eventCount++;

            if (!singleEvent) {
                yield();
            }
        }
        
        if (entry->status != entry->_nextStatus && !entryDeleted) {
            // Need to update server state

            polip_rpc_status_t oldStatus = entry->status;
            entry->status = entry->_nextStatus;
        
            polipCode = polip_rpc_workflow_push_status(
                rpcWkObj,
                entry,
                dev,
                doc,
                timestamp
            );

            if (polipCode == POLIP_OK) {
                // transition graph to next state, may free
                if (oldStatus == POLIP_RPC_STATUS_CANCELED) {
                    if (entry->status == POLIP_RPC_STATUS_REJECTED) {
                        // Will reappear as pending from server, fix state
                        entry->status = POLIP_RPC_STATUS_PENDING;
                        entry->_nextStatus = POLIP_RPC_STATUS_PENDING;
                    } else if (entry->status == POLIP_RPC_STATUS_ACKNOWLEDGED) {
                        // Canceled, should free
                        polip_rpc_workflow_free_rpc(rpcWkObj, entry);
                        entryDeleted = true;
                    }
                } else if (entry->status == POLIP_RPC_STATUS_SUCCESS 
                        || entry->status == POLIP_RPC_STATUS_FAILURE 
                        || entry->status == POLIP_RPC_STATUS_REJECTED) {
                    // Must have transitioned from a valid state into final, should free
                    polip_rpc_workflow_free_rpc(rpcWkObj, entry);
                    entryDeleted = true;
                } else if (entry->status == _RPC_STATUS_UNKNOWN) {
                    // Error occurred, call error handler then free RPC
                    rpcWkObj->hooks.workflowErrorCb(dev, doc, POLIP_WORKFLOW_PUSH_RPC);
                    polip_rpc_workflow_free_rpc(rpcWkObj, entry);
                    polipCode = POLIP_ERROR_WORKFLOW;
                    entryDeleted = true;
                }
            }

            eventCount++;

            if (!singleEvent) {
                yield();
            }
        }

        entry = nextEntry;
    }

    return polipCode;
}

polip_ret_code_t polip_rpc_workflow_poll_event(polip_rpc_workflow_t* rpcWkObj, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp) {

    // Flipping this state, to catch non-changed rpc._checked fields
    rpcWkObj->state._masterCheckedBit = !rpcWkObj->state._masterCheckedBit;

    JsonArray array = doc["rpc"].as<JsonArray>();
    for(JsonObject rpcObj : array) {
        String uuid = rpcObj["uuid"]; 
        String type = rpcObj["type"];
        polip_rpc_status_t status = polip_rpc_status_str2enum(((String)rpcObj["status"]).c_str());
        JsonObject paramObj = rpcObj["parameters"];

        // check if uuid in list
        bool found = false;
        for (polip_rpc_t* entry = rpcWkObj->state._activePtr; entry != NULL; entry = entry._nextPtr) {
            if (uuid == entry->uuid) {
                found = true;
                entry->_checked = rpcWkObj->state._masterCheckedBit;

                if (status == POLIP_RPC_STATUS_CANCELED) {
                    if (rpcWkObj->hooks.cancelRPC(dev, entry)) {
                        POLIP_RPC_WORKFLOW_ACKNOWLEDGE_RPC(rpcWkObj, entry);
                    } else {
                        POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
                    }

                } else if (status == POLIP_RPC_STATUS_PENDING) {
                    // somehow it got set back to pending state?
                    bool accepted = false;

                    // Try optional reaccept hook, otherwise fallback to accept hook
                    if (rpcWkObj->hooks.reacceptRPC != NULL) {
                        accepted = rpcWkObj->hooks.reacceptRPC(dev, entry, paramObj);
                    } else {
                        accepted = rpcWkObj->hooks.acceptRPC(dev, entry, paramObj);
                    }

                    if (accepted) {
                        POLIP_RPC_WORKFLOW_ACKNOWLEDGE_RPC(rpcWkObj, entry);
                    } else {
                        POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
                    }

                } else if (status != POLIP_RPC_STATUS_PENDING) {
                    POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
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
        if (rpcWkObj->state.numActiveRPCs < rpcWkObj->params.maxActivedRPCs && rpcWkObj->state.allowingNewRPCs) {

            // Add RPC to active list
            polip_rpc_t* entry = polip_rpc_workflow_new_rpc(
                rpcWkObj, 
                status, 
                uuid.c_str(), 
                type.c_str(), 
                paramObj, 
                dev
            );

            // Handle different states that RPC in could be on first check
            if (status == POLIP_RPC_STATUS_PENDING) {
                // If comes in as pending then accept and ack
                if (rpcWkObj->hooks.acceptRPC(dev, entry, paramObj)) {
                    POLIP_RPC_WORKFLOW_ACKNOWLEDGE_RPC(rpcWkObj, entry);
                } else {
                    POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
                }

            } else if (status == POLIP_RPC_STATUS_CANCELED) {
                // If comes in as cancel, then accept and cancel
                if (rpcWkObj->hooks.cancelRPC(dev, entry)) {
                    POLIP_RPC_WORKFLOW_ACKNOWLEDGE_RPC(rpcWkObj, entry);
                } else {
                    POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
                }

            } else {
                // Already in some weird state, just reject the RPC
                POLIP_RPC_WORKFLOW_REJECT_RPC(rpcWkObj, entry);
            }
        } else {
            // Can't add this RPC to list so continue on to next in server's list to see if we need to process it
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

polip_rpc_t* polip_rpc_workflow_new_rpc(polip_rpc_workflow_t* rpcWkObj, polip_rpc_status_t status, 
        const char* uuid, const char* type, JsonObject& paramObj, polip_device_t* dev) {
    if (rpcWkObj->state._freePtr == NULL) {
        return NULL;    // No RPC available
    } else if (strlen(uuid)+1 > POLIP_RPC_TYPE_BUFFER_SIZE 
            || strlen(type)+1 > POLIP_RPC_TYPE_BUFFER_SIZE) {
        return NULL;    // Data too large - probably malformed
    }

    polip_rpc_t* rpcPtr = rpcWkObj->state._freePtr;
    rpcWkObj->state._freePtr = rpcPtr._nextPtr;
    
    rpcPtr._nextPtr = rpcWkObj->state._activePtr;
    rpcWkObj->state._activePtr = rpcPtr;

    rpcPtr->status = status;
    rpcPtr->_nextStatus = status;
    rpcPtr->_checked = rpcWkObj->state._masterCheckedBit;
    strcpy(rpcPtr->uuid, uuid);
    strcpy(rpcPtr->type, type);
    rpcPtr->userContext = NULL;

    if (rpcWkObj->hooks.newRPC != NULL) {
        rpcWkObj->hooks.newRPC(dev, rpcPtr, paramObj);
    }

    rpcWkObj->state.numActiveRPCs++;
    return rpcPtr;
}

bool polip_rpc_workflow_free_rpc(polip_rpc_workflow_t* rpcWkObj, polip_rpc_t* rpc) {
    if (rpcWkObj->state._activePtr == NULL || rpc == NULL) {
        return false; // No active or rpc is NULL, so nothing to free
    }

    if (rpcWkObj->hooks.freeRPC != NULL) {
        rpcWkObj->hooks.freeRPC(dev, rpcPtr);
    }

    bool status = false;
    if (rpc == rpcWkObj->state._activePtr) {
        // First element case - easy
        rpcWkObj->state._activePtr = rpc._nextPtr;
        rpc._nextPtr = rpcWkObj->state._freePtr;
        rpcWkObj->state._freePtr = rpc;
        status = true;

    } else {
        // Need to search within list (start at child of 0th element)
        for (polip_rpc_t* parent = rpcWkObj->state._activePtr; parent != NULL; parent = parent._nextPtr) {
            if (parent._nextPtr == rpc) {
                // Found slice point
                parent._nextPtr = rpc._nextPtr;
                rpc._nextPtr = rpcWkObj->state._freePtr;
                rpcWkObj->state._freePtr = rpc;
                status = true;
                break;
            }
        }
    }

    if (status) {
        rpcWkObj->state.numActiveRPCs--;
    }

    return status;
}

polip_rpc_t* polip_rpc_workflow_get_rpc_by_uuid(polip_rpc_workflow_t* rpcWkObj, const char* uuid) {
    for (polip_rpc_t* entry = rpcWkObj->state._activePtr; entry != NULL; entry = entry._nextPtr) {
        if (strcmp(entry->uuid, uuid) == 0) {
            return entry;
        }
    }

    return NULL; // Could not find a matching entry
}

polip_ret_code_t polip_rpc_workflow_push_status(polip_rpc_workflow_t* rpcWkObj, polip_rpc_t* rpc, polip_device_t* dev, 
        JsonDocument& doc, const char* timestamp) {

    doc.clear();

    JsonObject rpcObj = doc.createNestedObject("rpc");
    rpcObj["uuid"] = rpc->uuid;
    rpcObj["result"] = nullptr;
    rpcObj["status"] = polip_rpc_status_enum2str(rpc->status); 

    if (rpcWkObj->hooks.pushRPCSetup != NULL) {
        rpcWkObj->hooks.pushRPCSetup(dev, rpc, doc);
    }

    polip_ret_code_t polipCode = polip_pushRPC(
        dev,
        doc,
        timestamp
    );

    if (polipCode == POLIP_OK) {
        if (rpcWkObj->hooks.pushRPCResponse != NULL) {
            rpcWkObj->hooks.pushRPCResponse(dev, rpc, doc);
        }

        if (rpcWkObj->params.pushAdditionalNotification) {
            doc.clear();

            if (rpcWkObj->hooks.pushNotifactionSetup != NULL) {
                rpcWkObj->hooks.pushNotifactionSetup(dev, rpc, doc);
            } else if (rpcWkObj->hooks.workflowErrorCb != NULL) {
                rpcWkObj->hooks.workflowErrorCb(dev, doc, POLIP_WORKFLOW_PUSH_RPC);
            }

            polipCode = polip_pushNotification(
                dev,
                doc,
                timestamp
            );

            if (polipCode == POLIP_OK) {    
                if (rpcWkObj->hooks.pushNotifactionResponse != NULL) {
                    rpcWkObj->hooks.pushNotifactionResponse(dev, rpc, doc);
                } else if (rpcWkObj->hooks.workflowErrorCb != NULL) {
                    rpcWkObj->hooks.workflowErrorCb(dev, doc, POLIP_WORKFLOW_PUSH_RPC);
                }
            }
        }
    }

    return polipCode;
} 