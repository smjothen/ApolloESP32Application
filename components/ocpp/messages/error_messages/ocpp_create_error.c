#include "ocpp_json/ocppj_message_structure.h"
#include "messages/error_messages/ocpp_call_error.h"

cJSON * ocpp_create_call_error(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details){

	cJSON * message = cJSON_CreateArray();
	if(message == NULL){
		return NULL;
	}

	cJSON * message_type_id = cJSON_CreateNumber(eOCPPJ_MESSAGE_ID_ERROR);
	if(message_type_id == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, message_type_id);

	cJSON * unique_id_json = cJSON_CreateString(unique_id);
	if(unique_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, unique_id_json);


	cJSON * error_code_json = cJSON_CreateString(error_code);
	if(error_code_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, error_code_json);

	cJSON * error_description_json = cJSON_CreateString(error_description);
	if(error_code_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, error_description_json);

	if(error_details == NULL){
		cJSON * error_details_local = cJSON_CreateObject();
		if(error_details_local == NULL){
			goto error;
		}
		cJSON_AddItemToArray(message, error_details_local);

	}else{
		cJSON_AddItemToArray(message, error_details);
	}

	return message;
error:
	cJSON_Delete(message);
	return NULL;
}

