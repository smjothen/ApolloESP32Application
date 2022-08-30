#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_data_transfer_status.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_data_transfer_confirmation(const char * unique_id, const char * status, const char * data){

	if(status == NULL || ocpp_validate_enum(status, true, 4,
							OCPP_DATA_TRANSFER_STATUS_ACCEPTED,
							OCPP_DATA_TRANSFER_STATUS_REJECTED,
							OCPP_DATA_TRANSFER_STATUS_UNKNOWN_MESSAGE_ID,
							OCPP_DATA_TRANSFER_STATUS_UNKNOWN_VENDOR_ID) != 0)
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

	cJSON * data_json = cJSON_CreateString(data);
	if(status_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "data", data_json);

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
