#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_charge_point_error_code.h"
#include "types/ocpp_charge_point_status.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_status_notification_request(unsigned int connector_id, const char * error_code, const char * info, const char * status, time_t timestamp, const char * vendor_id, const char * vendor_error_code){

	if(connector_id < 1)
		return NULL;

	if(ocpp_validate_enum(error_code, 16,
				OCPP_CP_ERROR_CONNECTOR_LOCK_FAILURE,
				OCPP_CP_ERROR_EV_COMMUNICATION_ERROR,
				OCPP_CP_ERROR_GROUND_FAILURE,
				OCPP_CP_ERROR_HIGH_TEMPERATURE,
				OCPP_CP_ERROR_INTERNAL_ERROR,
				OCPP_CP_ERROR_LOCAL_LIST_CONFLICT,
				OCPP_CP_ERROR_NO_ERROR,
				OCPP_CP_ERROR_OTHER_ERROR,
				OCPP_CP_ERROR_OVER_CURRENT_FAILURE,
				OCPP_CP_ERROR_OVER_VOLTAGE,
				OCPP_CP_ERROR_POWER_METER_FAILURE,
				OCPP_CP_ERROR_POWER_SWITCH_FAILURE,
				OCPP_CP_ERROR_READER_FAILURE,
				OCPP_CP_ERROR_RESET_FAILURE,
				OCPP_CP_ERROR_UNDER_VOLTAGE,
				OCPP_CP_ERROR_WEAK_SIGNAL) != 0){
		return NULL;
	}

	if(info != NULL && !is_ci_string_type(info, 50))
		return NULL;

	if(ocpp_validate_enum(status, 9,
				OCPP_CP_STATUS_AVAILABLE,
				OCPP_CP_STATUS_PREPARING,
				OCPP_CP_STATUS_CHARGING,
				OCPP_CP_STATUS_SUSPENDED_EVSE,
				OCPP_CP_STATUS_SUSPENDED_EV,
				OCPP_CP_STATUS_FINISHING,
				OCPP_CP_STATUS_RESERVED,
				OCPP_CP_STATUS_UNAVAILABLE,
				OCPP_CP_STATUS_FAULTED) != 0){
		return NULL;
	}

	if(vendor_id != NULL && !is_ci_string_type(vendor_id, 255))
		return NULL;

	if(vendor_error_code != NULL && !is_ci_string_type(vendor_error_code, 50))
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * connector_id_json = cJSON_CreateNumber(connector_id);
	if(connector_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "connectorId", connector_id_json);

	cJSON * error_code_json = cJSON_CreateString(error_code);
	if(error_code_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "errorCode", error_code_json);

	if(info != NULL){
		cJSON * info_json = cJSON_CreateString(info);
		if(info_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "info", info_json);
	}

	cJSON * status_json = cJSON_CreateString(status);
	if(status_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "status", status_json);

	if(timestamp != 0){
		char timestamp_buffer[30];
		size_t written_length = strftime(timestamp_buffer, sizeof(timestamp_buffer), "%FT%T%Z", localtime(&timestamp));
		if(written_length == 0)
			goto error;

		cJSON * timestamp_json = cJSON_CreateString(timestamp_buffer);
		if(timestamp_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "timestamp", timestamp_json);
	}

	if(vendor_id != NULL){
		cJSON * vendor_id_json = cJSON_CreateString(vendor_id);
		if(vendor_id_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "vendorId", vendor_id_json);
	}

	if(vendor_error_code != NULL){
		cJSON * vendor_error_json = cJSON_CreateString(vendor_error_code);
		if(vendor_error_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "vendorErrorConde", vendor_error_json);
	}

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_STATUS_NOTIFICATION, payload);
	if(result == NULL)
		goto error;

	return result;

error:
	cJSON_Delete(payload);
	return NULL;
}
