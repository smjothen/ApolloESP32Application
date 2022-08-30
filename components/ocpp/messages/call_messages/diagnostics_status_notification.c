#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_diagnostics_status.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_diagnostics_status_notification_request(const char * status){
	if(ocpp_validate_enum(status, true, 4,
				OCPP_DIAGNOSTICS_STATUS_IDLE,
				OCPP_DIAGNOSTICS_STATUS_UPLOADED,
				OCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED,
				OCPP_DIAGNOSTICS_STATUS_UPLOADING) != 0){
		return NULL;
	}

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * status_json = cJSON_CreateString(status);
	if(status_json == NULL){
		cJSON_Delete(payload);
		NULL;
	}
	cJSON_AddItemToObject(payload, "status", status_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_DIAGNOSTICS_STATUS_NOTIFICATION, payload);
	if(result == NULL){
		cJSON_Delete(payload);
		return NULL;
	}
	else{
		return result;
	}
}
