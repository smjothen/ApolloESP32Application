#ifndef OCPP_CALL_RESULT_CB_H
#define OCPP_CALL_RESULT_CB_H

#include "cJSON.h"

/** @file
 * @brief Contains callback for CS initiated CallResult.
 */

/**
 * @brief callback for CallResult
 *
 * @param unique_id "This must be the exact same ID that is in the call request so that the recipient can match request
 * and result"
 * @param payload "Payload is a JSON object containing the results of the executed Action. If there is no payload JSON
 * allows for two different notations: null or and empty object {}. Although it seems trivial we consider it good practice to only use the empty
 * object statement. Null usually represents something undefined, which is not the same as empty, and also {} is shorter"
 * @param cb_data additional data not spcified in ocpp.
 */
typedef void (*ocpp_result_callback) (const char * unique_id, cJSON * payload, void * cb_data);

#endif /*OCPP_CALL_RESULT_CB_H*/
