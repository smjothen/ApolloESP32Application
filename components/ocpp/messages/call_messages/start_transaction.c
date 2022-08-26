#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_date_time.h"

cJSON * ocpp_create_start_transaction_request(unsigned int connector_id, const char * id_tag, int meter_start, int * reservation_id, time_t timestamp){
	if(connector_id < 1)
		return NULL;

	if(id_tag == NULL || !is_ci_string_type(id_tag, 20))
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * connector_id_json = cJSON_CreateNumber(connector_id);
	if(connector_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "connectorId", connector_id_json);

	cJSON * id_tag_json = cJSON_CreateString(id_tag);
	if(id_tag_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "idTag", id_tag_json);

	cJSON * meter_start_json = cJSON_CreateNumber(meter_start);
	if(meter_start_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "meterStart", meter_start_json);

	if(reservation_id != NULL){
		cJSON * reservation_id_json = cJSON_CreateNumber(*reservation_id);
		if(reservation_id_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "reservationId", reservation_id_json);
	}

	char timestamp_buffer[30];
	size_t written_length = ocpp_print_date_time(timestamp, timestamp_buffer, sizeof(timestamp_buffer));
	if(written_length == 0)
		goto error;

	cJSON * timestamp_json = cJSON_CreateString(timestamp_buffer);
	if(timestamp_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "timestamp", timestamp_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_START_TRANSACTION, payload);
	if(result == NULL){
		goto error;
	}
	return result;

error:
	cJSON_Delete(payload);
	return NULL;
}
