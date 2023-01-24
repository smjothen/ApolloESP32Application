#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_get_diagnostics_confirmation(const char * unique_id, const char * file_name){

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	if(file_name != NULL){
		if(is_ci_string_type(file_name, 255)){
			cJSON * file_name_json = cJSON_CreateString(file_name);
			if(file_name_json == NULL)
				goto error;

			cJSON_AddItemToObject(payload, "fileName", file_name_json);
		}else{
			goto error;
		}
	}

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
