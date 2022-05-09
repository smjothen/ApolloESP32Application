#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_meter_value.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_reason.h"

cJSON * ocpp_create_stop_transaction_request(const char * id_tag, int meter_stop, time_t timestamp, int transaction_id, const char * reason, size_t meter_values_count, struct ocpp_meter_value * transaction_data){

	if(id_tag != NULL && !is_ci_string_type(id_tag, 20))
		return NULL;

	if(reason != NULL && ocpp_validate_enum(reason, 11,
							OCPP_REASON_DE_AUTHORIZED,
							OCPP_REASON_EMERGENCY_STOP,
							OCPP_REASON_EV_DISCONNECT,
							OCPP_REASON_HARD_RESET,
							OCPP_REASON_LOACL,
							OCPP_REASON_OTHER,
							OCPP_REASON_POWER_LOSS,
							OCPP_REASON_REBOOT,
							OCPP_REASON_REMOTE,
							OCPP_REASON_SOFT_RESET,
							OCPP_REASON_UNLOCK_COMMAND) != 0){
		return NULL;
	}

	if(meter_values_count > 0 && transaction_data == NULL)
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;


	if(id_tag != NULL){
		cJSON * id_tag_json = cJSON_CreateString(id_tag);
		if(id_tag_json == NULL){
			cJSON_Delete(payload);
			NULL;
		}
		cJSON_AddItemToObject(payload, "idTag", id_tag_json);
	}

	cJSON * meter_stop_json = cJSON_CreateNumber(meter_stop);
	if(meter_stop_json == NULL){
		cJSON_Delete(payload);
		NULL;
	}
	cJSON_AddItemToObject(payload, "meterStop", meter_stop_json);

	char timestamp_buffer[30];
	size_t written_length = strftime(timestamp_buffer, sizeof(timestamp_buffer), "%FT%T%Z", localtime(&timestamp));
	if(written_length != 0)
		goto error;

	cJSON * timestamp_json = cJSON_CreateString(timestamp_buffer);
	if(timestamp_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "timestamp", timestamp_json);

	cJSON * transaction_id_json = cJSON_CreateNumber(transaction_id);
	if(transaction_id_json == NULL){
		cJSON_Delete(payload);
		NULL;
	}
	cJSON_AddItemToObject(payload, "transactionId", transaction_id_json);

	if(reason != NULL){
		cJSON * reason_json = cJSON_CreateString(reason);
		if(reason_json == NULL){
			cJSON_Delete(payload);
			NULL;
		}
		cJSON_AddItemToObject(payload, "reason", reason_json);
	}

	cJSON * meter_values_json = cJSON_CreateArray();
	if(meter_values_json == NULL)
		goto error;

	for(size_t i = 0; i < meter_values_count; i++){
		cJSON * value_json = create_meter_value_json(transaction_data[i]);
		if(value_json == NULL){
			cJSON_Delete(meter_values_json);
			goto error;
		}
		else{
			cJSON_AddItemToArray(meter_values_json, value_json);
		}
	}
	cJSON_AddItemToObject(payload, "meterValue", meter_values_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_STOP_TRANSACTION, payload);
	if(result == NULL)
		goto error;

	return result;

error:
	cJSON_Delete(payload);
	return NULL;

}
