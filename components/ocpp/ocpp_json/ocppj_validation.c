#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "esp_log.h"

#include "ocpp_json/ocppj_validation.h"

static const char * TAG = "OCPPJ_VALIDATION";

enum ocppj_err_t ocppj_get_decimal_field(cJSON * container, const char * field_name, bool required, double * value_out,
					char * error_description_out, size_t error_description_length){

	if(cJSON_HasObjectItem(container, field_name)){
		cJSON * field_json = cJSON_GetObjectItem(container, field_name);

		if(cJSON_IsNumber(field_json)){
			*value_out = field_json->valuedouble;
			return eOCPPJ_NO_ERROR;
		}else{
			snprintf(error_description_out, error_description_length, "Expected '%s' to be decimal", field_name);
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}

	}else if(required){
		snprintf(error_description_out, error_description_length, "Expected '%s' field", field_name);
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}else{
		return eOCPPJ_NO_VALUE;
	}
}

enum ocppj_err_t ocppj_get_int_field(cJSON * container, const char * field_name, bool required, int * value_out,
				char * error_description_out, size_t error_description_length){

	// cJSON stores numbers as both integer and double. Even if the interger is set the original value may
	// be a double. We therefore get the double value to check if it is an actual int.
	double decimal_value;
	enum ocppj_err_t result = ocppj_get_decimal_field(container, field_name, required, &decimal_value,
						error_description_out, error_description_length);

	char * error_update_ptr = NULL;

	switch(result){
	case eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION:
		error_update_ptr = strstr(error_description_out, "decimal");
		if(error_update_ptr != NULL){
			memcpy(error_update_ptr, "integer", sizeof(char) * 7); // Don't copy '\0'. 7 is length of "integer" AND "decimal"
		}
		return result;

	case eOCPPJ_NO_VALUE:
		return result;

	case eOCPPJ_NO_ERROR:
		if(decimal_value >= INT_MIN && decimal_value <= INT_MAX && decimal_value == round(decimal_value)){
			*value_out = (int)decimal_value;
			return eOCPPJ_NO_ERROR;
		}else{
			snprintf(error_description_out, error_description_length, "Expected '%s' to be integer", field_name);
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}
	default:
		ESP_LOGE(TAG, "Unexpected ocpp json parsing error: '%s'", ocppj_error_code_from_id(result));
		return result;
	}
}

enum ocppj_err_t ocppj_get_string_field(cJSON * container, const char * field_name, bool required, char ** value_out,
				char * error_description_out, size_t error_description_length){

	if(cJSON_HasObjectItem(container, field_name)){

		*value_out = cJSON_GetStringValue(cJSON_GetObjectItem(container, field_name));
		if(*value_out == NULL){
			snprintf(error_description_out, error_description_length, "Expected '%s' to be string type", field_name);
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}

		return eOCPPJ_NO_ERROR;

	}else if(required){
		snprintf(error_description_out, error_description_length, "Expected '%s' field", field_name);
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}else{
		return eOCPPJ_NO_VALUE;
	}
}
