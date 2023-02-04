/**
 * @file polip-client.cpp
 * @author Curt Henrichs
 * @brief Polip Client 
 * @version 0.1
 * @date 2022-10-20
 * 
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

//==============================================================================
//  Libraries
//==============================================================================

#include "polip-client.hpp"

//==============================================================================
//  Preprocessor Constants
//==============================================================================

#define ARBITRARY_MSG_BUFFER_SIZE   (512)   //! Serialized c-string max size
#define INTERNAL_DOC_BUFFER_SIZE    (256)   //! JSON Obj buffer size

//==============================================================================
//  Data Structure Declaration
//==============================================================================

typedef struct _ret {
    int httpCode;                       //! HTTP server status on POST
    DeserializationError jsonCode;      //! Serializer status on deserialization
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

//==============================================================================
//  Public Function Implementation
//==============================================================================

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

polip_ret_code_t polip_getValue(polip_device_t* dev, const char* timestamp) {
    StaticJsonDocument<INTERNAL_DOC_BUFFER_SIZE> doc;

    polip_ret_code_t status = _requestTemplate(dev, doc, timestamp, 
        POLIP_DEVICE_INGEST_SERVER_URL "/api/v1/device/value", 
        true, // skip value in request pack 
        true  // skip tag in request pack
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

    if (ret.jsonCode != DeserializationError::Ok) {
        return POLIP_ERROR_RESPONSE_DESERIALIZATION;
    }

    if (ret.httpCode != 200) {
        if (doc.containsKey("data") && 0 == strcmp(doc["data"], "value invalid")) {
            return POLIP_ERROR_VALUE_MISMATCH;
        } else {
            return POLIP_ERROR_SERVER_ERROR;
        }
    }

    if (!dev->skipTagCheck) {
        const char* oldTag = doc["tag"]; //TODO need to copy out
        doc["tag"] = "0";
        _computeTag(dev->keyStr, dev->keyStrLen, doc);

        if (0 != strcmp(oldTag, doc["tag"])) { // Tag match failed
            return POLIP_ERROR_TAG_MISMATCH;   
        }
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
        dev->value += 1;
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
    char buffer[ARBITRARY_MSG_BUFFER_SIZE];
    WiFiClient client;
    HTTPClient http;

    http.begin(client, endpoint);
    http.addHeader("Content-Type", "text/json");

    serializeJson(doc, buffer);
    retVal.httpCode = http.POST(buffer);

    doc.clear();
    retVal.jsonCode = deserializeJson(doc, http.getString());

    http.end();

    return retVal;
}

static void _computeTag(const uint8_t* key, int keyLen, JsonDocument& doc) {
    char buffer[ARBITRARY_MSG_BUFFER_SIZE];

    SHA256HMAC hmac(key, keyLen);
    serializeJson(doc, buffer);
    hmac.doUpdate(buffer);

    uint8_t authCode[SHA256HMAC_SIZE];
    hmac.doFinal(authCode);

    doc["tag"] = authCode; // TODO probably need to copy this into heap / doc
}