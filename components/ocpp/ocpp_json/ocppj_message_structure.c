#include "esp_log.h"
#include "ocpp_json/ocppj_message_structure.h"

static const char * TAG = "OCPPJ_STRUCTURE";

const char * ocppj_error_code_from_id(enum ocppj_err_t error_id){

	switch(error_id){
	case eOCPPJ_ERROR_NOT_IMPLEMENTED:
		return OCPPJ_ERROR_NOT_IMPLEMENTED;
	case eOCPPJ_ERROR_NOT_SUPPORTED:
		return OCPPJ_ERROR_NOT_SUPPORTED;
	case eOCPPJ_ERROR_INTERNAL:
		return OCPPJ_ERROR_INTERNAL;
	case eOCPPJ_ERROR_PROTOCOL:
		return OCPPJ_ERROR_PROTOCOL;
	case eOCPPJ_ERROR_SECURITY:
		return OCPPJ_ERROR_SECURITY;
	case eOCPPJ_ERROR_FORMATION_VIOLATION:
		return OCPPJ_ERROR_FORMATION_VIOLATION;
	case eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION:
		return OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	case eOCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION:
		return OCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION;
	case eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION:
		return OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
	case eOCPPJ_ERROR_GENERIC:
		return OCPPJ_ERROR_GENERIC;

	case eOCPPJ_NO_ERROR:
	case eOCPPJ_NO_VALUE:
		ESP_LOGE(TAG, "Attempt to get error when no error occured");
		return NULL;
	default:
		ESP_LOGE(TAG, "Unhandled error code");
		return NULL;
	}
}

const char * ocppj_get_string_from_message(cJSON * call, unsigned int index){
	return cJSON_GetStringValue(cJSON_GetArrayItem(call, index));
}
