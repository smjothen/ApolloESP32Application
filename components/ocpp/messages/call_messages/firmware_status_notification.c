#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_firmware_status.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_firmware_status_notification_request(const char * status){
	if(ocpp_validate_enum(status, 7,
				OCPP_FIRMWARE_STATUS_DOWNLOADED,
				OCPP_FIRMWARE_STATUS_DOWNLOAD_FAILED,
				OCPP_FIRMWARE_STATUS_DOWNLOADING,
				OCPP_FIRMWARE_STATUS_IDLE,
				OCPP_FIRMWARE_STATUS_INSTALLATION_FAILED,
				OCPP_FIRMWARE_STATUS_INSTALLING,
				OCPP_FIRMWARE_STATUS_INSTALLED) != 0){
		return NULL;
	}

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * status_json = cJSON_CreateString(status);
	if(status_json == NULL){
		cJSON_Delete(payload);
		NULL;
	}
	cJSON_AddItemToObject(payload, "status", status_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_FIRMWARE_STATUS_NOTIFICATION, payload);
	if(result == NULL){
		cJSON_Delete(payload);
		return NULL;
	}
	else{
		return result;
	}
}
