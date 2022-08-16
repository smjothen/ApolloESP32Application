#include <stdbool.h>
#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_create_meter_value.h"

cJSON * ocpp_create_meter_values_request(unsigned int connector_id, const int * transaction_id, struct ocpp_meter_value_list * meter_values){
	if(meter_values == NULL)
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	cJSON * connector_id_json = cJSON_CreateNumber(connector_id);
	if(connector_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(payload, "connectorId", connector_id_json);

	if(transaction_id != NULL){
		cJSON * transaction_id_json = cJSON_CreateNumber(*transaction_id);
		if(transaction_id_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(payload, "transactionId", transaction_id_json);
	}

	cJSON * meter_values_json = cJSON_CreateArray();
	if(meter_values_json == NULL)
		goto error;

	bool contains_meter_value = false;
	while(meter_values != NULL){
		if(meter_values->value != NULL){
			cJSON * value_json = create_meter_value_json(*meter_values->value);
			if(value_json == NULL){
				cJSON_Delete(meter_values_json);
				goto error;
			}
			contains_meter_value = true;
			cJSON_AddItemToArray(meter_values_json, value_json);
		}
		meter_values = meter_values->next;
	}

	if(!contains_meter_value){
		cJSON_Delete(meter_values_json);
		goto error;
	}
	cJSON_AddItemToObject(payload, "meterValue", meter_values_json);

	cJSON * result =  ocpp_create_call(OCPPJ_ACTION_METER_VALUES, payload);
	if(result == NULL){
		cJSON_Delete(payload);
		return NULL;
	}
	else{
		return result;
	}

error:
	cJSON_Delete(payload);
	return NULL;
}
