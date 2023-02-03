#ifndef OCPP_CALL_WITH_CB_H
#define OCPP_CALL_WITH_CB_H

#include <stdbool.h>
#include "cJSON.h"

#include "messages/error_messages/ocpp_call_error_cb.h"
#include "messages/result_messages/ocpp_call_result_cb.h"

struct ocpp_call_with_cb{
	cJSON * call_message;
	ocpp_result_callback result_cb;
	ocpp_error_callback error_cb;
	void * cb_data;
};

void free_call_with_cb(struct ocpp_call_with_cb * call);
bool check_call_with_cb_validity(struct ocpp_call_with_cb * call);
#endif /*OCPP_CALL_WITH_CB_H*/
