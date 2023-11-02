/**
 * @file polip-device.cpp
 * @author Curt Henrichs
 * @brief Polip Device Client Implementation 
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib communicates with Okos Polip home automation server via a 
 * device leve JSON API. 
 * 
 * Implemented within this module is the various endpoints to pull/push data
 */

//==============================================================================
//  Libraries
//==============================================================================

#include "./polip-device.hpp"

//==============================================================================
//  Data Structure Declaration
//==============================================================================

/**
 * Packed return structure for internal http requests
 */
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

polip_ret_code_t polip_checkServerStatus() {

    WiFiClient client;
    HTTPClient http;

    http.begin(client, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/health/check");
    int code = http.GET();
    http.end();

    return (code == 200) ? POLIP_OK : POLIP_ERROR_SERVER_ERROR;
}

polip_ret_code_t polip_getState(polip_device_t* dev, JsonDocument& doc, const char* timestamp, 
        bool queryState, bool queryManufacturer, bool queryRPC) {

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/poll" "?state=%s&manufacturer=%s&rpc=%s",
        (queryState) ? "true" : "false",
        (queryManufacturer) ? "true" : "false",
        (queryRPC) ? "true" : "false"
    );

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_getMeta(polip_device_t* dev, JsonDocument& doc, const char* timestamp,
        bool queryState, bool querySensors, bool queryManufacturer, bool queryGeneral) {

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/meta" "?state=%s&manufacturer=%s&sensors=%s&general=%s",
        (queryState) ? "true" : "false",
        (queryManufacturer) ? "true" : "false",
        (querySensors) ? "true" : "false",
        (queryGeneral) ? "true" : "false"
    );

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_pushState(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("state")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/state");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_pushError(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("message") || !doc.containsKey("code")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/error");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_pushSensors(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("sense")) {
        return POLIP_ERROR_LIB_REQUEST;
    }

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/sense");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_getValue(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/device/v1/value");

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, uri,
        true, // skip value in request pack 
        true  // skip tag in request pack, response check
    );

    if (status == POLIP_OK) {
        dev->value = doc["value"];
    }
    
    return status;
}

polip_ret_code_t polip_pushRPC(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    if (!doc.containsKey("rpc")) {
        return POLIP_ERROR_LIB_REQUEST;
    } else if (!doc["rpc"].containsKey("uuid")) {
        return POLIP_ERROR_LIB_REQUEST;
    } else if (!doc["rpc"].containsKey("result")) {
        return POLIP_ERROR_LIB_REQUEST;
    } else if (!doc["rpc"].containsKey("status")) {
        return POLIP_ERROR_LIB_REQUEST;
    } 

    if (!doc["rpc"].containsKey("timestamp")) {
        // Append timestamp only if not explicitly provided
        doc["rpc"]["timestamp"] = timestamp;
    }

    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/rpc");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_getSchema(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/schema");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_getAllErrorSemantics(polip_device_t* dev, JsonDocument& doc, const char* timestamp) {
    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/error/semantic");

    return _requestTemplate(dev, doc, timestamp, uri);
}

polip_ret_code_t polip_getErrorSemanticFromCode(polip_device_t* dev, int32_t code, JsonDocument& doc, const char* timestamp) {
    char uri[POLIP_QUERY_URI_BUFFER_SIZE];
    sprintf(uri, POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/error/semantic" "?code=%d", code);

    return _requestTemplate(dev, doc, timestamp, uri);
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