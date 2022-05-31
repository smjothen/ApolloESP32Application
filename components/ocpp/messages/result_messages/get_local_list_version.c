#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_update_status.h"

cJSON * ocpp_create_get_local_list_version_confirmation(const char * unique_id, int listVersion){

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * version_json = cJSON_CreateNumber(listVersion);
	if(version_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "listVersion", version_json);

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
