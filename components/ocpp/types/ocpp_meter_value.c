#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_log.h"
#include "types/ocpp_meter_value.h"
#include "types/ocpp_create_meter_value.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_date_time.h"

static const char * TAG = "OCPP M_VALUE";

static cJSON * create_sampled_value_json(struct ocpp_sampled_value_list sample){
	struct ocpp_sampled_value_list * item = &sample;

	cJSON * result = cJSON_CreateArray();
	while(item != NULL){

		if(item->value->context[0] != '\0' && ocpp_validate_enum(item->value->context, 8,
									OCPP_READING_CONTEXT_INTERRUPTION_BEGIN,
									OCPP_READING_CONTEXT_INTERRUPTION_END,
									OCPP_READING_CONTEXT_OTHER,
									OCPP_READING_CONTEXT_SAMPLE_CLOCK,
									OCPP_READING_CONTEXT_SAMPLE_PERIODIC,
									OCPP_READING_CONTEXT_TRANSACTION_BEGIN,
									OCPP_READING_CONTEXT_TRANSACTION_END,
									OCPP_READING_CONTEXT_TRIGGER) != 0){
			ESP_LOGE(TAG, "Invalid context");
			goto error;
		}
		if(item->value->format[0] != '\0' && ocpp_validate_enum(item->value->format, 2,
								OCPP_VALUE_FORMAT_RAW,
								OCPP_VALUE_FORMAT_SIGNED_DATA) != 0){
			ESP_LOGE(TAG, "Invalid format");
			goto error;
		}

		if(item->value->measurand[0] != '\0' && ocpp_validate_enum(item->value->measurand, 22,
									OCPP_MEASURAND_CURRENT_EXPORT,
									OCPP_MEASURAND_CURRENT_IMPORT,
									OCPP_MEASURAND_CURRENT_OFFERED,
									OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER,
									OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER,
									OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER,
									OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER,
									OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL,
									OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
									OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL,
									OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL,
									OCPP_MEASURAND_FREQUENCY,
									OCPP_MEASURAND_POWER_ACTIVE_EXPORT,
									OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
									OCPP_MEASURAND_POWER_FACTOR,
									OCPP_MEASURAND_POWER_OFFERED,
									OCPP_MEASURAND_POWER_REACTIVE_EXPORT,
									OCPP_MEASURAND_POWER_REACTIVE_IMPORT,
									OCPP_MEASURAND_RPM,
									OCPP_MEASURAND_SOC,
									OCPP_MEASURAND_TEMPERATURE,
									OCPP_MEASURAND_VOLTAGE) != 0){
			ESP_LOGE(TAG, "Invalid measurand");
			goto error;
		}

		if(item->value->phase[0] != '\0' && ocpp_validate_enum(item->value->phase, 10,
								OCPP_PHASE_L1,
								OCPP_PHASE_L2,
								OCPP_PHASE_L3,
								OCPP_PHASE_N,
								OCPP_PHASE_L1_N,
								OCPP_PHASE_L2_N,
								OCPP_PHASE_L3_N,
								OCPP_PHASE_L1_L2,
								OCPP_PHASE_L2_L3,
								OCPP_PHASE_L3_L1) != 0){
			ESP_LOGE(TAG, "Invalid phase");
			goto error;
		}

		if(item->value->location[0] != '\0' && ocpp_validate_enum(item->value->location, 5,
									OCPP_LOCATION_BODY,
									OCPP_LOCATION_CABLE,
									OCPP_LOCATION_EV,
									OCPP_LOCATION_INLET,
									OCPP_LOCATION_OUTLET) != 0){
			ESP_LOGE(TAG, "Invalid location");
			goto error;
		}

		if(item->value->unit[0] != '\0' && ocpp_validate_enum(item->value->unit, 15,
								OCPP_UNIT_OF_MEASURE_WH,
								OCPP_UNIT_OF_MEASURE_KWH,
								OCPP_UNIT_OF_MEASURE_VARH,
								OCPP_UNIT_OF_MEASURE_KVARH,
								OCPP_UNIT_OF_MEASURE_W,
								OCPP_UNIT_OF_MEASURE_KW,
								OCPP_UNIT_OF_MEASURE_VA,
								OCPP_UNIT_OF_MEASURE_KVA,
								OCPP_UNIT_OF_MEASURE_KVAR,
								OCPP_UNIT_OF_MEASURE_A,
								OCPP_UNIT_OF_MEASURE_V,
								OCPP_UNIT_OF_MEASURE_CELSIUS,
								OCPP_UNIT_OF_MEASURE_FAHRENHEIT,
								OCPP_UNIT_OF_MEASURE_K,
								OCPP_UNIT_OF_MEASURE_PERCENT) != 0){
			ESP_LOGE(TAG, "Invalid unit");
			goto error;
		}

		cJSON * sample_json = cJSON_CreateObject();
		if(result == NULL)
			goto error;

		cJSON * value_json = cJSON_CreateString(item->value->value);
		if(value_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(sample_json, "value", value_json);

		if(item->value->context[0] != '\0'){
			cJSON * context_json = cJSON_CreateString(item->value->context);
			if(context_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "context", context_json);
		}
		if(item->value->format[0] != '\0'){
			cJSON * format_json = cJSON_CreateString(item->value->format);
			if(format_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "format", format_json);
		}
		if(item->value->measurand[0] != '\0'){
			cJSON * measurand_json = cJSON_CreateString(item->value->measurand);
			if(measurand_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "measurand", measurand_json);
		}
		if(item->value->phase[0] != '\0'){
			cJSON * phase_json = cJSON_CreateString(item->value->phase);
			if(phase_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "phase", phase_json);
		}
		if(item->value->location[0] != '\0'){
			cJSON * location_json = cJSON_CreateString(item->value->location);
			if(location_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "location", location_json);
		}
		if(item->value->unit[0] != '\0'){
			cJSON * unit_json = cJSON_CreateString(item->value->unit);
			if(unit_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "unit", unit_json);
		}

		cJSON_AddItemToArray(result, sample_json);
		item = item->next;
	}

	return result;
error:
	cJSON_Delete(result);
	return NULL;
}

cJSON * create_meter_value_json(struct ocpp_meter_value meter_value){
	char timestamp[30];

	size_t written_length = ocpp_print_date_time(meter_value.timestamp, timestamp, sizeof(timestamp));
	if(written_length == 0)
		return NULL;


	cJSON * result = cJSON_CreateObject();
	if(result == NULL)
		return NULL;

	cJSON * timestamp_json = cJSON_CreateString(timestamp);
	if(timestamp_json == NULL){
		cJSON_Delete(result);
		return NULL;
	}
	cJSON_AddItemToObject(result, "timestamp", timestamp_json);

	cJSON * sampled_json = create_sampled_value_json(meter_value.sampled_value);
	if(sampled_json == NULL){
		cJSON_Delete(result);
		return NULL;
	}
	cJSON_AddItemToObject(result, "sampledValue", sampled_json);

	return result;
}

void ocpp_sampled_list_delete(struct ocpp_sampled_value_list list){

	free(list.value);

	struct ocpp_sampled_value_list * current = list.next;
	struct ocpp_sampled_value_list * next = NULL;

	while(current != NULL){
		next = current->next;
		current->next = NULL;

		free(current->value);
		current->value = NULL;

		free(current);
		current = next;
	}
}

struct ocpp_sampled_value_list * ocpp_sampled_list_add(struct ocpp_sampled_value_list * list, struct ocpp_sampled_value value){
	while(list->next != NULL){
		list = list->next;
	}

	if(list->value != NULL){
		list->next = calloc(sizeof(struct ocpp_sampled_value_list), 1);
		if(list->next == NULL){
			ESP_LOGE(TAG, "Unable to malloc for new list node");
			return NULL;
		}
		list = list->next;
	}

	list->value = malloc(sizeof(struct ocpp_sampled_value));
	if(list->value == NULL){
		ESP_LOGE(TAG, "Unable to allocate for new list value");
		return NULL;
	}

	memcpy(list->value, &value, sizeof(struct ocpp_sampled_value));

	return list;
}

size_t ocpp_sampled_list_get_length(struct ocpp_sampled_value_list * list){

	size_t length = 0;

	if(list->value == NULL)
		return length;
	length++;

	while(list->next != NULL){
		length++;
		list = list->next;
	}

	return length;
}

struct ocpp_sampled_value_list * ocpp_sampled_list_get_last(struct ocpp_sampled_value_list * list){
	while(list->next != NULL)
		list = list->next;

	return list;
}
