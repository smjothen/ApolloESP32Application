#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_authorize_request(const char * id_tag){
	if(!is_ci_string_type(id_tag, 20))
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * id_tag_json = cJSON_CreateString(id_tag);
	if(id_tag_json == NULL){
		cJSON_Delete(payload);
		NULL;
	}
	cJSON_AddItemToObject(payload, "idTag", id_tag_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_AUTORIZE, payload);
	if(result == NULL){
		cJSON_Delete(payload);
		return NULL;
	}
	else{
		return result;
	}
}
