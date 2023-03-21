#include <stdlib.h>

#include "esp_log.h"

#include "ocpp_call_with_cb.h"

static const char * TAG = "OCPP CALLBACK  ";

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

void error_logger(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data){
	const char * action = (cb_data != NULL) ? (const char *) cb_data : "No cb";
	char * error_details_str = error_details != NULL ? cJSON_PrintUnformatted(error_details) : NULL;

	ESP_LOGE(TAG, "[%s|%s]: (%s) '%s' '%s'", action, unique_id != NULL ? unique_id : "MISSING_ID",
		error_code != NULL ? error_code : "MISSING_ERROR_CODE", error_description != NULL ? error_description : "",
		error_details_str != NULL ? error_details_str : "");

	free(error_details_str);
}

void result_logger(const char * unique_id, cJSON * payload, void * cb_data){
	const char * action = (cb_data != NULL) ? (const char *) cb_data : "No cb";
	char * payload_str = payload != NULL ? cJSON_PrintUnformatted(payload) : NULL;

	ESP_LOGI(TAG, "[%s|%s]: '%s'", action, unique_id != NULL ? unique_id : "MISSING_ID", payload_str != NULL ? payload_str : "");

	free(payload_str);
}
