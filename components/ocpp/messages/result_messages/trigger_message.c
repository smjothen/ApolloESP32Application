#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_trigger_message_status.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_trigger_message_confirmation(const char * unique_id, const char * status){
	if(status == NULL || ocpp_validate_enum(status, true, 3,
							OCPP_TRIGGER_MESSAGE_STATUS_ACCEPTED,
							OCPP_TRIGGER_MESSAGE_STATUS_REJECTED,
							OCPP_TRIGGER_MESSAGE_STATUS_NOT_IMPLEMENTED))
	{
		return NULL;
	}

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * status_json = cJSON_CreateString(status);
	if(status_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "status", status_json);

	cJSON * result = ocpp_create_call_result(unique_id, payload);

	if(result == NULL){
		goto error;
	}
	else{
		return result;
	}

error:
	cJSON_Delete(payload);
	return NULL;
}
