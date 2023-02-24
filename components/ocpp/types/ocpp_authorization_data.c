#include <string.h>

#include "types/ocpp_authorization_data.h"

#include "ocpp_json/ocppj_message_structure.h"
#include "ocpp_json/ocppj_validation.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_id_tag_info.h"

enum ocppj_err_t ocpp_authorization_data_from_json(cJSON * data, struct ocpp_authorization_data * authorization_data_out,
						char * error_description_out, size_t error_description_length){

	char * value_string;
	enum ocppj_err_t err = ocppj_get_string_field(data, "idTag", true, &value_string, error_description_out, error_description_length);
	if(err != eOCPPJ_NO_ERROR)
		return err;

	if(!is_ci_string_type(value_string, 20)){
		strncpy(error_description_out, "'idTag' is not a valid CiString20Type", error_description_length);
		return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	}else{
		strcpy(authorization_data_out->id_tag, value_string);
	}

	if(cJSON_HasObjectItem(data, "idTagInfo")){
		err = id_tag_info_from_json(cJSON_GetObjectItem(data, "idTagInfo"), &(authorization_data_out->id_tag_info),
							error_description_out, error_description_length);
	}else{
		strcpy(authorization_data_out->id_tag_info.status, "DELETE");
	}

	return err;
}
