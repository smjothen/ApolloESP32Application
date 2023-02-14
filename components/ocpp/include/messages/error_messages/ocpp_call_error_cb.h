#ifndef OCPP_CALL_ERROR_CB_H
#define OCPP_CALL_ERROR_CB_H

#include "cJSON.h"

/** @file
 * @brief Contains callback for CS initiated CallError.
 */

/**
 * @brief Callback for CallError
 *
 * @param unique_id "This must be the exact same id that is in the call request so that the recipient can match request and result"
 * @param error_code "This field must contain a string from the ErrorCode table"
 * @param error_description "Should be filled in if possible, otherwise a clear empty string “”"
 * @param error_details "This JSON object describes error details in an undefined way. If there are no error details you MUST fill in an empty object \{}."
 * @param cb_data additional data not specified in ocpp
 */
typedef void (*ocpp_error_callback) (const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data);

#endif /*OCPP_CALL_ERROR_CB_H*/
