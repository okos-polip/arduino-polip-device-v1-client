/**
 * @file polip-core.hpp
 * @author Curt Henrichs
 * @brief Polip Client
 * @version 0.1
 * @date 2022-10-20
 * @copyright Copyright (c) 2022
 * 
 * Polip-lib to communicate with Okos Polip home automation server.
 */

#ifndef POLIP_CORE_HPP
#define POLIP_CORE_HPP

//==============================================================================
//  Preprocessor Constants
//==============================================================================

//! Allows POLIP lib to print out debug information on serial bus
#ifndef POLIP_VERBOSE_DEBUG
#define POLIP_VERBOSE_DEBUG                         (true)
#endif

#define POLIP_LIB_VERSION                           POLIP_VERSION_STD_FORMAT(0,0,1)

//==============================================================================
//  Preprocessor Macros
//==============================================================================

/**
 * Standard format for hardware and firmware version strings 
 */
#define POLIP_VERSION_STD_FORMAT(major,minor,patch) ("v" #major "." #minor "." #patch)

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
    POLIP_ERROR_WORKFLOW,
    POLIP_ERROR_MISSING_HOOK
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

//==============================================================================

#endif //POLIP_CORE_HPP