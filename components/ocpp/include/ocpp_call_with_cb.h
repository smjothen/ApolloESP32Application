#ifndef OCPP_CALL_WITH_CB_H
#define OCPP_CALL_WITH_CB_H

#include <stdbool.h>
#include "cJSON.h"

#include "messages/error_messages/ocpp_call_error_cb.h"
#include "messages/result_messages/ocpp_call_result_cb.h"

/** @file
 * @brief Contains structure and functions to deal with an OCPP req call.
 *
 * In ocpp a call expects either a CallResult or CallError as a reply.
 */

/**
 * @brief a call with callbacks for both CallResult and CallError
 */
struct ocpp_call_with_cb{
	cJSON * call_message; ///< .req call
	ocpp_result_callback result_cb; ///< function to be called if a matching CallResult is recieved
	ocpp_error_callback error_cb; ///< function to be called if a matching CallError is recieved
	void * cb_data; ///< Data to be given as a parameter to the callback function
	bool is_trigger_message; ///< True if message was created to respond to a TriggerMessage.req
};

/**
 * @brief free a ocpp_call_with_cb and its message
 *
 * @param call the call to be freed
 */
void free_call_with_cb(struct ocpp_call_with_cb * call);

/**
 * @brief check if a call contains a message that comply with the OCPP json specification for a Call.
 *
 * @param call The call to be checked
 * @return true if call is valid, false if not
 */
bool check_call_with_cb_validity(struct ocpp_call_with_cb * call);

/**
 * @brief Default error_cb used if error_cb is set to NULL.
 *
 * @details Logs the id, error code and description if not NULL. If cb_data is not null then it is expected to be a const char * with a tag used for logging
 */
void error_logger(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data);

/**
 * @brief Default result_logger used if result_cb is set to NULL.
 *
 * @details Logs the id. If cb_data is not null then it is expected to be a const char * with a tag used for logging
 */
void result_logger(const char * unique_id, cJSON * payload, void * cb_data);

#endif /*OCPP_CALL_WITH_CB_H*/
