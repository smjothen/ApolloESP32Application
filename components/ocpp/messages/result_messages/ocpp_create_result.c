#include "ocpp_json/ocppj_message_structure.h"
#include "messages/result_messages/ocpp_call_result.h"

cJSON * ocpp_create_call_result(const char * unique_id, cJSON * payload){

	cJSON * message = cJSON_CreateArray();
	if(message == NULL){
		return NULL;
	}

	cJSON * message_type_id = cJSON_CreateNumber(eOCPPJ_MESSAGE_ID_RESULT);
	if(message_type_id == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, message_type_id);

	cJSON * unique_id_json = cJSON_CreateString(unique_id);
	if(unique_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, unique_id_json);

	if(payload == NULL){
		cJSON * payload_local = cJSON_CreateObject();
		if(payload_local == NULL){
			goto error;
		}
		cJSON_AddItemToArray(message, payload_local);

	}else{
		/* if(cJSON_AddItemReferenceToArray(message, payload) != cJSON_bool) */
		/* 	goto error; */
		cJSON_AddItemReferenceToArray(message, payload);
	}

	return message;

error:
	cJSON_Delete(message);
	return NULL;
}

