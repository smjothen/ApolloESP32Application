#ifndef OCPP_CALL_WITH_CB_H
#define OCPP_CALL_WITH_CB_H

#include "cJSON.h"

#include "messages/error_messages/ocpp_call_error_cb.h"
#include "messages/result_messages/ocpp_call_result_cb.h"

struct ocpp_call_with_cb{
	cJSON * call_message;
	ocpp_result_callback result_cb;
	ocpp_error_callback error_cb;
	void * cb_data;
};

#endif /*OCPP_CALL_WITH_CB_H*/
