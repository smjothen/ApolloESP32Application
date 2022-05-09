#include "types/ocpp_key_value.h"
#include "types/ocpp_ci_string_type.h"

cJSON * create_key_value_json(struct ocpp_key_value key_value){
	if(!is_ci_string_type(key_value.key, 50))
		return NULL;

	if(!is_ci_string_type(key_value.value, 500))
		return NULL;

	cJSON * result = cJSON_CreateObject();
	if(result == NULL)
		return NULL;

	cJSON * key_json = cJSON_CreateString(key_value.key);
	if(key_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(result, "key", key_json);

	cJSON_AddBoolToObject(result, "readonly", key_value.readonly);

	cJSON * value_json = cJSON_CreateString(key_value.value);
	if(value_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(result, "value", value_json);

	return result;
error:
	cJSON_Delete(result);
	return NULL;
}
