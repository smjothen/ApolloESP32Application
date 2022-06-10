#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_firmware_status.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_unlock_connector_confirmation(const char * unique_id, const char * status){

	if(status == NULL || ocpp_validate_enum(status, 7,
							OCPP_FIRMWARE_STATUS_DOWNLOADED,
							OCPP_FIRMWARE_STATUS_DOWNLOAD_FAILED,
							OCPP_FIRMWARE_STATUS_DOWNLOADING,
							OCPP_FIRMWARE_STATUS_IDLE,
							OCPP_FIRMWARE_STATUS_INSTALLATION_FAILED,
							OCPP_FIRMWARE_STATUS_INSTALLING,
							OCPP_FIRMWARE_STATUS_INSTALLED) != 0)
	{
		return NULL;
	}

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * status_json = cJSON_CreateString(status);
	if(status_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "status", status_json);

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