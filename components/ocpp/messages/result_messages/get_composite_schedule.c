#include <string.h>

#include "messages/result_messages/ocpp_call_result.h"
#include "types/ocpp_get_composite_schedule_status.h"
#include "types/ocpp_date_time.h"
#include "types/ocpp_enum.h"

cJSON * ocpp_create_get_composite_schedule_confirmation(const char * unique_id, const char * status, int * connector_id, time_t * schedule_start, struct ocpp_charging_schedule * charging_schedule){

	if(status == NULL || ocpp_validate_enum(status, true, 2,
							OCPP_GET_COMPOSITE_SCHEDULE_STATUS_ACCEPTED,
							OCPP_GET_COMPOSITE_SCHEDULE_STATUS_REJECTED) != 0)
	{
		return NULL;
	}

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	if(cJSON_AddStringToObject(payload, "status", status) == NULL)
		goto error;

	if(connector_id != NULL && cJSON_AddNumberToObject(payload, "connectorId", *connector_id) == NULL){
		goto error;
	}

	if(schedule_start != NULL){
		char timestamp_buffer[30];
		size_t written_length = ocpp_print_date_time(*schedule_start, timestamp_buffer, sizeof(timestamp_buffer));
		if(written_length == 0)
			goto error;

		if(cJSON_AddStringToObject(payload, "scheduleStart", timestamp_buffer) == NULL)
			goto error;

	}else if(strcmp(status, OCPP_GET_COMPOSITE_SCHEDULE_STATUS_ACCEPTED) == 0){
		goto error;
	}

	if(charging_schedule != NULL){
		cJSON * charging_schedule_json = ocpp_create_charging_schedule_json(charging_schedule);
		if(charging_schedule_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "chargingSchedule", charging_schedule_json);

	}else if(strcmp(status, OCPP_GET_COMPOSITE_SCHEDULE_STATUS_ACCEPTED) == 0){
		goto error;
	}

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
