#include <stdlib.h>

#include "ocpp_call_with_cb.h"

void free_call_with_cb(struct ocpp_call_with_cb * call){
	if(call != NULL){
		free(call->call_message);
		free(call);
	}
}

bool check_call_with_cb_validity(struct ocpp_call_with_cb * call){
	if(call == NULL
		|| call->call_message == NULL || !cJSON_IsArray(call->call_message)
		|| cJSON_GetArraySize(call->call_message) < 3 || cJSON_GetArraySize(call->call_message) > 4){
		return false;
	}else{
		return true;
	}
}
