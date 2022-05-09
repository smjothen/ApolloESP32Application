#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_data_transfer_request(const char * vendor_id, const char * message_id, const char * data){
	if(!is_ci_string_type(vendor_id, 255))
		return NULL;

	if(message_id != NULL)
		if(!is_ci_string_type(message_id, 50))
			return NULL;

	//data is text of undefined length and is not validated here

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * vendor_id_json = cJSON_CreateString(vendor_id);
	if(vendor_id_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "vendorId", vendor_id_json);

	if(message_id != NULL){
		cJSON * message_id_json = cJSON_CreateString(message_id);
		if(message_id_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "messageId", message_id_json);
	}

	if(data != NULL){
		cJSON * data_json = cJSON_CreateString(data);
		if(data_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "data", data_json);
	}

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_DATA_TRANSFER, payload);
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
