#include <string.h>
#include <stdio.h>

#include "types/ocpp_id_tag_info.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_authorization_status.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_date_time.h"

enum ocppj_err_t id_tag_info_from_json(cJSON * idTagInfo, struct ocpp_id_tag_info * id_tag_out,
				char * error_description, size_t description_length){

	if(!cJSON_HasObjectItem(idTagInfo, "status")){
		snprintf(error_description, description_length, "Expected 'status' field");
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}

	cJSON * status_json = cJSON_GetObjectItem(idTagInfo, "status");

	if(!cJSON_IsString(status_json) || ocpp_validate_enum(status_json->valuestring, true, 5,
								OCPP_AUTHORIZATION_STATUS_ACCEPTED,
								OCPP_AUTHORIZATION_STATUS_BLOCKED,
								OCPP_AUTHORIZATION_STATUS_EXPIRED,
								OCPP_AUTHORIZATION_STATUS_INVALID,
								OCPP_AUTHORIZATION_STATUS_CONCURRENT_TX) != 0){

		snprintf(error_description, description_length, "Expected 'status' to be a valid AuthorizationStatus");
		return 	eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
	}
	strcpy(id_tag_out->status, status_json->valuestring);

	if(cJSON_HasObjectItem(idTagInfo, "parentIdTag")){
		cJSON * parent_id_tag_json = cJSON_GetObjectItem(idTagInfo, "parentIdTag");

		if(!cJSON_IsString(parent_id_tag_json) || !is_ci_string_type(parent_id_tag_json->valuestring, 20)){
			snprintf(error_description, description_length, "'parentIdTag' is not a valid idToken (CiString20Type)");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;

		}else{
			strcpy(id_tag_out->parent_id_tag, parent_id_tag_json->valuestring);
		}
	}else{
		id_tag_out->parent_id_tag[0] = '\0';
	}

	if(cJSON_HasObjectItem(idTagInfo, "expiryDate")){
		cJSON * expiry_date_json = cJSON_GetObjectItem(idTagInfo, "expiryDate");
		time_t expiry_date = (time_t)-1;

		if(cJSON_IsString(expiry_date_json))
			expiry_date = ocpp_parse_date_time(expiry_date_json->valuestring);

		if(expiry_date == (time_t)-1){
			snprintf(error_description, description_length, "'expiryDate' is not in a recognised time format");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}

		id_tag_out->expiry_date = expiry_date;

	}else{
		id_tag_out->expiry_date = (time_t)-1;
	}

	return eOCPPJ_NO_ERROR;
}
