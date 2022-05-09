#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_key_value.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_get_configuration_confirmation(const char * unique_id, size_t configuration_key_count, struct ocpp_key_value * configuration_key, size_t unknown_key_count, const char ** unknown_key){


	if(configuration_key_count > 0 && configuration_key == NULL)
		return NULL;

	if(unknown_key_count > 0 && unknown_key == NULL)
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	if(configuration_key_count > 0){
		cJSON * configuration_key_json = cJSON_CreateArray();
		for(size_t i = 0; i < configuration_key_count; i++){
			cJSON * key = create_key_value_json(configuration_key[i]);
			if(key == NULL){
				cJSON_Delete(configuration_key_json);
				goto error;
			}
			cJSON_AddItemToArray(configuration_key_json, key);
		}
		cJSON_AddItemToObject(payload, "configurationKey", configuration_key_json);
	}

	if(unknown_key_count > 0){
		cJSON * unknown_key_json = cJSON_CreateArray();
		for(size_t i = 0; i < unknown_key_count; i++){
			if(!is_ci_string_type(unknown_key[i], 50)){
				cJSON_Delete(unknown_key_json);
				goto error;
			}

			cJSON * key_json = cJSON_CreateString(unknown_key[i]);
			if(key_json == NULL){
				cJSON_Delete(unknown_key_json);
				goto error;
			}
			cJSON_AddItemToArray(unknown_key_json, key_json);
		}
		cJSON_AddItemToObject(payload, "unknownKey", unknown_key_json);
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
