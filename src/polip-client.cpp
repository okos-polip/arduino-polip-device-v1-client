/**
 * @file polip-client.cpp
 * @author Curt Henrichs
 * @brief Polip Client 
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

//==============================================================================
//  Libraries
//==============================================================================

#include "polip-client.hpp"

//==============================================================================
//  Data Structure Declaration
//==============================================================================

typedef struct _ret {
    int httpCode;                       //! HTTP server status on POST
    bool jsonCode;                      //! Serializer status on deserialization
} _ret_t;

//==============================================================================
//  Private Function Prototypes
//==============================================================================

static polip_ret_code_t _requestTemplate(polip_device_t* dev, JsonDocument& doc, 
        const char* timestamp, const char* endpoint, bool skipValue = false, 
        bool skipTag = false);
static void _packRequest(polip_device_t* dev, JsonDocument& doc, 
        const char* timestamp, bool skipValue = false, bool skipTag = false);
static _ret_t _sendPostRequest(JsonDocument& doc, const char* endpoint);
static void _computeTag(const uint8_t* key, int keyLen, JsonDocument& doc);
static void _array2string(uint8_t array[], unsigned int len, char buffer[]);

//==============================================================================
//  Public Function Implementation
//==============================================================================

polip_ret_code_t polip_workflow_initialize(polip_workflow_t* wkObj, unsigned long currentTime_ms) {
    wkObj->timers.pollTime = currentTime_ms;
    wkObj->timers.senseTime = currentTime_ms;
    return POLIP_OK;
}

polip_ret_code_t polip_workflow_periodic_update(polip_workflow_t* wkObj, 
        JsonDocument& doc, const char* timestamp, unsigned long currentTime_ms) {
    polip_ret_code_t retStatus = POLIP_OK;
    unsigned int eventCount = 0;

    // Push state to sever
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
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFFLOW_PUSH_STATE);
            }
        }

        eventCount++;
    }

    // Poll server for state changes
    if (!wkObj->flags.stateChanged && ((currentTime_ms - wkObj->timers.pollTime) >= wkObj->params.pollStateTimeThreshold) 
            && !(wkObj->params.onlyOneEvent && wkObj->flags.getValue && (eventCount >= 1))) {
        
        // Poll state from server
        doc.clear();
        polip_ret_code_t polipCode = polip_getState(
            wkObj->device,
            doc, 
            timestamp
        );

        // Process response
        if (polipCode == POLIP_ERROR_VALUE_MISMATCH) {
            wkObj->flags.getValue = true;
            
        } else if (polipCode == POLIP_OK) {
            wkObj->timers.pollTime = currentTime_ms;
            if (wkObj->hooks.pollStateRespCb != NULL) {
                wkObj->hooks.pollStateRespCb(wkObj->device, doc);
            }

        } else {
            wkObj->flags.error = polipCode;
            retStatus = POLIP_ERROR_WORKFLOW;
            if (wkObj->hooks.workflowErrorCb != NULL) {
                wkObj->hooks.workflowErrorCb(wkObj->device, doc, POLIP_WORKFLOW_POLL_STATE);
            }
        }

        eventCount++;
    }

    // Push sensor state to server
    if ((wkObj->flags.senseChanged || (wkObj->params.pushSensePeriodic 
            && (currentTime_ms - wkObj->timers.senseTime) >= wkObj->params.pushSenseTimeThreshold)) 
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
            wkObj->timers.senseTime = currentTime_ms;
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
    }

    return retStatus;
}

polip_ret_code_t polip_checkServerStatus() {

    WiFiClient client;
    HTTPClient http;

    http.begin(client, POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/");
    int code = http.GET();
    http.end();

    return (code == 200) ? POLIP_OK : POLIP_ERROR_SERVER_ERROR;
}

polip_ret_code_t polip_getState(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/poll?state=true"
    );

    return status;
}

polip_ret_code_t polip_pushState(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("state")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/push"
    );

    return status;
}

polip_ret_code_t polip_pushError(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("message") || !doc.containsKey("code")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/error"
    );

    return status;
}

polip_ret_code_t polip_pushSensors(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("sense")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/sense"
    );

    return status;
}

polip_ret_code_t polip_getValue(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/value", 
        true, // skip value in request pack 
        true  // skip tag in request pack, response check
    );

    if (status == POLIP_OK) {
        dev->value = doc["value"];
    }
    
    return status;
}

//==============================================================================
//  Private Function Implementation
//==============================================================================

static polip_ret_code_t _requestTemplate(polip_device_t* dev, JsonDocument& doc, 
        const char* timestamp, const char* endpoint, bool skipValue, bool skipTag) {
    _packRequest(dev, doc, timestamp, skipValue, skipTag);
    _ret_t ret = _sendPostRequest(doc, endpoint);

    if (ret.jsonCode) {
        return POLIP_ERROR_RESPONSE_DESERIALIZATION;
    }

    if (ret.httpCode != 200) {
        String msg = doc.as<String>();
        if (msg.equals("value invalid")) {
            return POLIP_ERROR_VALUE_MISMATCH;
        } else {
            return POLIP_ERROR_SERVER_ERROR;
        }
    }

    if (!skipTag && !dev->skipTagCheck) {
        const char* oldTag = doc["tag"];
        doc["tag"] = "0";
        _computeTag(dev->keyStr, dev->keyStrLen, doc);

        if (0 != strcmp(oldTag, doc["tag"])) { // Tag match failed
            return POLIP_ERROR_TAG_MISMATCH;   
        }
    }

    if (!skipValue) {
        dev->value += 1;
    }

    return POLIP_OK; // Document updates returned by reference
}

static void _packRequest(polip_device_t* dev, JsonDocument& doc, 
        const char* timestamp, bool skipValue, bool skipTag) {

    doc["serial"] = dev->serialStr; 
    doc["firmware"] = dev->firmwareStr;
    doc["hardware"] = dev->hardwareStr;
    doc["timestamp"] = timestamp;

    if (!skipValue) {
        doc["value"] = dev->value;
    }

    if (!skipTag) {
        doc["tag"] = "0";
        if (!dev->skipTagCheck) {
            _computeTag(dev->keyStr, dev->keyStrLen, doc);
        }
    }
}

static _ret_t _sendPostRequest(JsonDocument& doc, const char* endpoint) {
    _ret_t retVal;
    char buffer[POLIP_ARBITRARY_MSG_BUFFER_SIZE];
    WiFiClient client;
    HTTPClient http;

    http.begin(client, endpoint);
    http.addHeader("Content-Type", "application/json");

    serializeJson(doc, buffer);

#if defined(POLIP_VERBOSE_DEBUG) && POLIP_VERBOSE_DEBUG
    Serial.print("Endpoint: ");
    Serial.println(endpoint);
    Serial.print("TX = ");
    Serial.println(buffer);
#endif

    retVal.httpCode = http.POST(buffer);

    doc.clear();
    retVal.jsonCode = deserializeJson(doc, http.getString());

#if defined(POLIP_VERBOSE_DEBUG) && POLIP_VERBOSE_DEBUG
    serializeJson(doc, buffer);
    Serial.print("RX = ");
    Serial.println(buffer);
#endif

    http.end();

    return retVal;
}

static void _computeTag(const uint8_t* key, int keyLen, JsonDocument& doc) {
    char buffer[POLIP_ARBITRARY_MSG_BUFFER_SIZE];

    SHA256HMAC hmac(key, keyLen);
    serializeJson(doc, buffer);
    hmac.doUpdate(buffer);

    uint8_t authCode[SHA256HMAC_SIZE];
    hmac.doFinal(authCode);

    char authStr[SHA256HMAC_SIZE*2 + 1];
    _array2string(authCode, SHA256HMAC_SIZE, authStr);

    doc["tag"] = authStr;
}

static void _array2string(uint8_t array[], unsigned int len, char buffer[]) {
    for (unsigned int i = 0; i < len; i++) {
        uint8_t nib1 = (array[i] >> 4) & 0x0F;
        uint8_t nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'a' + nib1 - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'a' + nib2 - 0xA;
    }
    buffer[len*2] = '\0';
}