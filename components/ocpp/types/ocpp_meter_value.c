#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <ctype.h>

#include "cJSON.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "types/ocpp_meter_value.h"
#include "types/ocpp_create_meter_value.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_date_time.h"

static const char * TAG = "OCPP M_VALUE";

static cJSON * create_sampled_value_json(struct ocpp_sampled_value_list * sample){
	struct ocpp_sampled_value_list * item = sample;

	cJSON * result = cJSON_CreateArray();

	char context[32];
	char format[16];
	char measurand[32];
	char phase[8];
	char location[8];
	char unit[16];

	while(item != NULL){

		strcpy(context, ocpp_reading_context_from_id(item->value->context));
		strcpy(format, ocpp_format_from_id(item->value->format));
		strcpy(measurand, ocpp_measurand_from_id(item->value->measurand));
		strcpy(phase, ocpp_phase_from_id(item->value->phase));
		strcpy(location, ocpp_location_from_id(item->value->location));
		strcpy(unit, ocpp_unit_from_id(item->value->unit));

		cJSON * sample_json = cJSON_CreateObject();
		if(result == NULL)
			goto error;

		cJSON * value_json = cJSON_CreateString(item->value->value);
		if(value_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(sample_json, "value", value_json);

		if(context[0] != '\0'){
			cJSON * context_json = cJSON_CreateString(context);
			if(context_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "context", context_json);
		}
		if(format[0] != '\0'){
			cJSON * format_json = cJSON_CreateString(format);
			if(format_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "format", format_json);
		}
		if(measurand[0] != '\0'){
			cJSON * measurand_json = cJSON_CreateString(measurand);
			if(measurand_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "measurand", measurand_json);
		}
		if(phase[0] != '\0'){
			cJSON * phase_json = cJSON_CreateString(phase);
			if(phase_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "phase", phase_json);
		}
		if(location[0] != '\0'){
			cJSON * location_json = cJSON_CreateString(location);
			if(location_json == NULL){
				goto error;
			}
			cJSON_AddItemToObject(sample_json, "location", location_json);
		}
		if(unit[0] != '\0'){
			cJSON * unit_json = cJSON_CreateString(unit);
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

	cJSON * sampled_value_json = create_sampled_value_json(meter_value.sampled_value);
	if(sampled_value_json == NULL){
		cJSON_Delete(result);
		return NULL;
	}
	cJSON_AddItemToObject(result, "sampledValue", sampled_value_json);

	return result;
}

struct ocpp_generic_list{
	void * value;
	struct ocpp_generic_list * next;
};

struct ocpp_sampled_value_list * ocpp_create_sampled_list(){

	return calloc(sizeof(struct ocpp_sampled_value_list), 1);
}

struct ocpp_meter_value_list * ocpp_create_meter_list(){

	return calloc(sizeof(struct ocpp_meter_value_list), 1);
}


void ocpp_sampled_list_delete(struct ocpp_sampled_value_list * list){
	if(list == NULL)
		return;

	struct ocpp_sampled_value_list * next = NULL;

	while(list != NULL){
		next = list->next;
		list->next = NULL;
		free(list->value);
		list->value = NULL;
		free(list);
		list = next;
	}
}

void ocpp_meter_list_delete(struct ocpp_meter_value_list * list){
	if(list == NULL)
		return;

	struct ocpp_meter_value_list * next = NULL;

	while(list != NULL){
		next = list->next;
		list->next = NULL;

		if(list->value != NULL && list->value->sampled_value != NULL){
			ocpp_sampled_list_delete(list->value->sampled_value);
		}

		free(list->value);
		list->value = NULL;

		list = next;
	}
}

struct ocpp_sampled_value_list * ocpp_sampled_list_add(struct ocpp_sampled_value_list * list, struct ocpp_sampled_value value){
	list = ocpp_sampled_list_get_last(list);

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
		ESP_LOGE(TAG, "Unable to allocate for new sampled list value");
		return NULL;
	}
	memcpy(list->value, &value, sizeof(struct ocpp_sampled_value));
	return list;
}

struct ocpp_meter_value_list * ocpp_meter_list_add(struct ocpp_meter_value_list * list, struct ocpp_meter_value value){
	list = ocpp_meter_list_get_last(list);
	if(list->value != NULL){
		list->next = calloc(sizeof(struct ocpp_meter_value_list), 1);
		if(list->next == NULL){
			ESP_LOGE(TAG, "Unable to malloc for new list node");
			return NULL;
		}
		list = list->next;
	}

	list->value = malloc(sizeof(struct ocpp_meter_value));
	if(list->value == NULL){
		ESP_LOGE(TAG, "Unable to allocate for new meter list value");
		return NULL;
	}

	list->value->timestamp = value.timestamp;
	list->value->sampled_value = ocpp_create_sampled_list();
	if(list->value->sampled_value == NULL){
		free(list->value);
		list->value = NULL;
		return NULL;
	}

	struct ocpp_sampled_value_list * samples = value.sampled_value;
	while(samples != NULL){
		if(samples->value != NULL)
			ocpp_sampled_list_add(list->value->sampled_value, *samples->value);
		samples = samples->next;
	}

	return list;
}

struct ocpp_meter_value_list * ocpp_meter_list_add_reference(struct ocpp_meter_value_list * list, struct ocpp_meter_value * value){
	list = ocpp_meter_list_get_last(list);
	if(list->value != NULL){
		list->next = calloc(sizeof(struct ocpp_meter_value_list), 1);
		if(list->next == NULL){
			ESP_LOGE(TAG, "Unable to malloc for new list node");
			return NULL;
		}
		list = list->next;
	}

	list->value = value;
	return list;
}

static size_t ocpp_generic_list_get_length(struct ocpp_generic_list * list){
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

size_t ocpp_sampled_list_get_length(struct ocpp_sampled_value_list * list){
	return ocpp_generic_list_get_length((struct ocpp_generic_list *) list);
}

size_t ocpp_meter_list_get_length(struct ocpp_meter_value_list * list){
	return ocpp_generic_list_get_length((struct ocpp_generic_list *) list);
}

struct ocpp_generic_list * ocpp_generic_list_get_last(struct ocpp_generic_list * list){
	while(list->next != NULL){
		list = list->next;
	}

	return list;

}

struct ocpp_sampled_value_list * ocpp_sampled_list_get_last(struct ocpp_sampled_value_list * list){
	return (struct ocpp_sampled_value_list *) ocpp_generic_list_get_last((struct ocpp_generic_list *) list);
}

struct ocpp_meter_value_list * ocpp_meter_list_get_last(struct ocpp_meter_value_list * list){
	return (struct ocpp_meter_value_list *) ocpp_generic_list_get_last((struct ocpp_generic_list *) list);
}

const char * ocpp_reading_context_from_id(enum ocpp_reading_context_id id){
	switch(id){
	case eOCPP_CONTEXT_INTERRUPTION_BEGIN:
		return OCPP_READING_CONTEXT_INTERRUPTION_BEGIN;
	case eOCPP_CONTEXT_INTERRUPTION_END:
		return OCPP_READING_CONTEXT_INTERRUPTION_END;
	case eOCPP_CONTEXT_OTHER:
		return OCPP_READING_CONTEXT_OTHER;
	case eOCPP_CONTEXT_SAMPLE_CLOCK:
		return OCPP_READING_CONTEXT_SAMPLE_CLOCK;
	case eOCPP_CONTEXT_SAMPLE_PERIODIC:
		return OCPP_READING_CONTEXT_SAMPLE_PERIODIC;
	case eOCPP_CONTEXT_TRANSACTION_BEGIN:
		return OCPP_READING_CONTEXT_TRANSACTION_BEGIN;
	case eOCPP_CONTEXT_TRANSACTION_END:
		return OCPP_READING_CONTEXT_TRANSACTION_END;
	case eOCPP_CONTEXT_TRIGGER:
		return OCPP_READING_CONTEXT_TRIGGER;
	default:
		return "";
	}
}

enum ocpp_reading_context_id ocpp_reading_context_to_id(const char * context){
	if(strcmp(context, OCPP_READING_CONTEXT_INTERRUPTION_BEGIN) == 0){
		return eOCPP_CONTEXT_INTERRUPTION_BEGIN;
	}else if(strcmp(context, OCPP_READING_CONTEXT_INTERRUPTION_END) == 0){
		return eOCPP_CONTEXT_INTERRUPTION_END;
	}else if(strcmp(context, OCPP_READING_CONTEXT_OTHER) == 0){
		return eOCPP_CONTEXT_OTHER;
	}else if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_CLOCK) == 0){
		return eOCPP_CONTEXT_SAMPLE_CLOCK;
	}else if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_PERIODIC) == 0){
		return eOCPP_CONTEXT_SAMPLE_PERIODIC;
	}else if(strcmp(context, OCPP_READING_CONTEXT_TRANSACTION_BEGIN) == 0){
		return eOCPP_CONTEXT_TRANSACTION_BEGIN;
	}else if(strcmp(context, OCPP_READING_CONTEXT_TRANSACTION_END) == 0){
		return eOCPP_CONTEXT_TRANSACTION_END;
	}else if(strcmp(context, OCPP_READING_CONTEXT_TRIGGER) == 0){
		return eOCPP_CONTEXT_TRIGGER;
	}else{
		return 0;
	}
}

const char * ocpp_format_from_id(enum ocpp_format_id id){
	switch(id){
	case eOCPP_FORMAT_RAW:
		return OCPP_VALUE_FORMAT_RAW;
	case eOCPP_FORMAT_SIGNED_DATA:
		return OCPP_VALUE_FORMAT_SIGNED_DATA;
	default:
		return "";
	}
}

enum ocpp_format_id ocpp_format_to_id(const char * format){
	if(strcmp(format, OCPP_VALUE_FORMAT_RAW) == 0){
		return eOCPP_FORMAT_RAW;
	}else if(strcmp(format, OCPP_VALUE_FORMAT_SIGNED_DATA) == 0){
		return eOCPP_FORMAT_SIGNED_DATA;
	}else{
		return 0;
	}
}

const char * ocpp_measurand_from_id(enum ocpp_measurand_id id){
	switch(id){
	case eOCPP_MEASURAND_CURRENT_EXPORT:
		return OCPP_MEASURAND_CURRENT_EXPORT;
	case eOCPP_MEASURAND_CURRENT_IMPORT:
		return OCPP_MEASURAND_CURRENT_IMPORT;
	case eOCPP_MEASURAND_CURRENT_OFFERED:
		return OCPP_MEASURAND_CURRENT_OFFERED;
	case eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER:
		return OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER;
	case eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER:
		return OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER;
	case eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER:
		return OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER;
	case eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER:
		return OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER;
	case eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL:
		return OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL;
	case eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL:
		return OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL;
	case eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL:
		return OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL;
	case eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL:
		return OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL;
	case eOCPP_MEASURAND_FREQUENCY:
		return OCPP_MEASURAND_FREQUENCY;
	case eOCPP_MEASURAND_POWER_ACTIVE_EXPORT:
		return OCPP_MEASURAND_POWER_ACTIVE_EXPORT;
	case eOCPP_MEASURAND_POWER_ACTIVE_IMPORT:
		return OCPP_MEASURAND_POWER_ACTIVE_IMPORT;
	case eOCPP_MEASURAND_POWER_FACTOR:
		return OCPP_MEASURAND_POWER_FACTOR;
	case eOCPP_MEASURAND_POWER_OFFERED:
		return OCPP_MEASURAND_POWER_OFFERED;
	case eOCPP_MEASURAND_POWER_REACTIVE_EXPORT:
		return OCPP_MEASURAND_POWER_REACTIVE_EXPORT;
	case eOCPP_MEASURAND_POWER_REACTIVE_IMPORT:
		return OCPP_MEASURAND_POWER_REACTIVE_IMPORT;
	case eOCPP_MEASURAND_RPM:
		return OCPP_MEASURAND_RPM;
	case eOCPP_MEASURAND_SOC:
		return OCPP_MEASURAND_SOC;
	case eOCPP_MEASURAND_TEMPERATURE:
		return OCPP_MEASURAND_TEMPERATURE;
	case eOCPP_MEASURAND_VOLTAGE:
		return OCPP_MEASURAND_VOLTAGE;
	default:
		return "";
	}
}

enum ocpp_measurand_id ocpp_measurand_to_id(const char * measurand){
	if(strcmp(measurand, OCPP_MEASURAND_CURRENT_EXPORT) == 0){
		return eOCPP_MEASURAND_CURRENT_EXPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_CURRENT_IMPORT) == 0){
		return eOCPP_MEASURAND_CURRENT_IMPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_CURRENT_OFFERED) == 0){
		return eOCPP_MEASURAND_CURRENT_OFFERED;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER) == 0){
		return eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER) == 0){
		return eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER) == 0){
		return eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER) == 0){
		return eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL) == 0){
		return eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL) == 0){
		return eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL) == 0){
		return eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL;
	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL) == 0){
		return eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL;
	}else if(strcmp(measurand, OCPP_MEASURAND_FREQUENCY) == 0){
		return eOCPP_MEASURAND_FREQUENCY;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_ACTIVE_EXPORT) == 0){
		return eOCPP_MEASURAND_POWER_ACTIVE_EXPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_ACTIVE_IMPORT) == 0){
		return eOCPP_MEASURAND_POWER_ACTIVE_IMPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_FACTOR ) == 0){
		return eOCPP_MEASURAND_POWER_FACTOR;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_OFFERED) == 0){
		return eOCPP_MEASURAND_POWER_OFFERED;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_REACTIVE_EXPORT) == 0){
		return eOCPP_MEASURAND_POWER_REACTIVE_EXPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_REACTIVE_IMPORT) == 0){
		return eOCPP_MEASURAND_POWER_REACTIVE_IMPORT;
	}else if(strcmp(measurand, OCPP_MEASURAND_RPM ) == 0){
		return eOCPP_MEASURAND_RPM;
	}else if(strcmp(measurand, OCPP_MEASURAND_SOC) == 0){
		return eOCPP_MEASURAND_SOC;
	}else if(strcmp(measurand, OCPP_MEASURAND_TEMPERATURE) == 0){
		return eOCPP_MEASURAND_TEMPERATURE;
	}else if(strcmp(measurand, OCPP_MEASURAND_VOLTAGE) == 0){
		return eOCPP_MEASURAND_VOLTAGE;
	}else{
		return 0;
	}
}

const char * ocpp_phase_from_id(enum ocpp_phase_id id){
	switch(id){
	case eOCPP_PHASE_L1:
		return OCPP_PHASE_L1;
	case eOCPP_PHASE_L2:
		return OCPP_PHASE_L2;
	case eOCPP_PHASE_L3:
		return OCPP_PHASE_L3;
	case eOCPP_PHASE_N:
		return OCPP_PHASE_N;
	case eOCPP_PHASE_L1_N:
		return OCPP_PHASE_L1_N;
	case eOCPP_PHASE_L2_N:
		return OCPP_PHASE_L2_N;
	case eOCPP_PHASE_L3_N:
		return OCPP_PHASE_L3_N;
	case eOCPP_PHASE_L1_L2:
		return OCPP_PHASE_L1_L2;
	case eOCPP_PHASE_L2_L3:
		return OCPP_PHASE_L2_L3;
	case eOCPP_PHASE_L3_L1:
		return OCPP_PHASE_L3_L1;
	default:
		return "";
	}
}

enum ocpp_phase_id ocpp_phase_to_id(const char * phase){
	if(strcmp(phase, OCPP_PHASE_L1) == 0){
		return eOCPP_PHASE_L1;
	}else if(strcmp(phase, OCPP_PHASE_L2) == 0){
		return eOCPP_PHASE_L2;
	}else if(strcmp(phase, OCPP_PHASE_L3) == 0){
		return eOCPP_PHASE_L3;
	}else if(strcmp(phase, OCPP_PHASE_N) == 0){
		return eOCPP_PHASE_N;
	}else if(strcmp(phase, OCPP_PHASE_L1_N) == 0){
		return eOCPP_PHASE_L1_N;
	}else if(strcmp(phase, OCPP_PHASE_L2_N) == 0){
		return eOCPP_PHASE_L2_N;
	}else if(strcmp(phase, OCPP_PHASE_L3_N) == 0){
		return eOCPP_PHASE_L3_N;
	}else if(strcmp(phase, OCPP_PHASE_L1_L2) == 0){
		return eOCPP_PHASE_L1_L2;
	}else if(strcmp(phase, OCPP_PHASE_L2_L3) == 0){
		return eOCPP_PHASE_L2_L3;
	}else if(strcmp(phase, OCPP_PHASE_L3_L1) == 0){
		return eOCPP_PHASE_L3_L1;
	}else{
		return 0;
	}
}

const char * ocpp_location_from_id(enum ocpp_location_id id){
	switch(id){
	case eOCPP_LOCATION_BODY:
		return OCPP_LOCATION_BODY;
	case eOCPP_LOCATION_CABLE:
		return OCPP_LOCATION_CABLE;
	case eOCPP_LOCATION_EV:
		return OCPP_LOCATION_EV;
	case eOCPP_LOCATION_INLET:
		return OCPP_LOCATION_INLET;
	case eOCPP_LOCATION_OUTLET:
		return OCPP_LOCATION_OUTLET;
	default:
		return "";
	}
}


enum ocpp_location_id ocpp_location_to_id(const char * location){
	if(strcmp(location, OCPP_LOCATION_BODY) == 0){
		return eOCPP_LOCATION_BODY;
	}else if(strcmp(location, OCPP_LOCATION_CABLE) == 0){
		return eOCPP_LOCATION_CABLE;
	}else if(strcmp(location, OCPP_LOCATION_EV) == 0){
		return eOCPP_LOCATION_EV;
	}else if(strcmp(location, OCPP_LOCATION_INLET) == 0){
		return eOCPP_LOCATION_INLET;
	}else if(strcmp(location, OCPP_LOCATION_OUTLET) == 0){
		return eOCPP_LOCATION_OUTLET;
	}else{
		return 0;
	}
}

const char * ocpp_unit_from_id(enum ocpp_unit_id id){
	switch(id){
	case eOCPP_UNIT_WH:
		return OCPP_UNIT_OF_MEASURE_WH;
	case eOCPP_UNIT_KWH:
		return OCPP_UNIT_OF_MEASURE_KWH;
	case eOCPP_UNIT_VARH:
		return OCPP_UNIT_OF_MEASURE_VARH;
	case eOCPP_UNIT_KVARH:
		return OCPP_UNIT_OF_MEASURE_KVARH;
	case eOCPP_UNIT_W:
		return OCPP_UNIT_OF_MEASURE_W;
	case eOCPP_UNIT_KW:
		return OCPP_UNIT_OF_MEASURE_KW;
	case eOCPP_UNIT_VA:
		return OCPP_UNIT_OF_MEASURE_VA;
	case eOCPP_UNIT_KVA:
		return OCPP_UNIT_OF_MEASURE_KVA;
	case eOCPP_UNIT_VAR:
		return OCPP_UNIT_OF_MEASURE_VAR;
	case eOCPP_UNIT_KVAR:
		return OCPP_UNIT_OF_MEASURE_KVAR;
	case eOCPP_UNIT_A:
		return OCPP_UNIT_OF_MEASURE_A;
	case eOCPP_UNIT_V:
		return OCPP_UNIT_OF_MEASURE_V;
	case eOCPP_UNIT_CELSIUS:
		return OCPP_UNIT_OF_MEASURE_CELSIUS;
	case eOCPP_UNIT_FAHRENHEIT:
		return OCPP_UNIT_OF_MEASURE_FAHRENHEIT;
	case eOCPP_UNIT_K:
		return OCPP_UNIT_OF_MEASURE_K;
	case eOCPP_UNIT_PERCENT:
		return OCPP_UNIT_OF_MEASURE_PERCENT;
	default:
		return "";
	}
}

enum ocpp_unit_id ocpp_unit_to_id(const char * unit){
	if(strcmp(unit, OCPP_UNIT_OF_MEASURE_WH) == 0){
		return eOCPP_UNIT_WH;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_KWH) == 0){
		return eOCPP_UNIT_KWH;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_VARH) == 0){
		return eOCPP_UNIT_VARH;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_KVARH) == 0){
		return eOCPP_UNIT_KVARH;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_W) == 0){
		return eOCPP_UNIT_W;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_KW) == 0){
		return eOCPP_UNIT_KW;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_VA) == 0){
		return eOCPP_UNIT_VA;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_KVA) == 0){
		return eOCPP_UNIT_KVA;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_VAR) == 0){
		return eOCPP_UNIT_VAR;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_KVAR) == 0){
		return eOCPP_UNIT_KVAR;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_A) == 0){
		return eOCPP_UNIT_A;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_V) == 0){
		return eOCPP_UNIT_V;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_CELSIUS) == 0){
		return eOCPP_UNIT_CELSIUS;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_FAHRENHEIT) == 0){
		return eOCPP_UNIT_FAHRENHEIT;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_K) == 0){
		return eOCPP_UNIT_K;
	}else if(strcmp(unit, OCPP_UNIT_OF_MEASURE_PERCENT) == 0){
		return eOCPP_UNIT_PERCENT;
	}else{
		return 0;
	}
}

size_t ocpp_sample_get_width(struct ocpp_sampled_value value){
	size_t width = sizeof(value.context) + sizeof(value.format) + sizeof(value.measurand)
		+ sizeof(value.phase) + sizeof(value.location) + sizeof(value.unit);

	size_t value_length = strnlen(value.value, sizeof(value.value));

	// If value does not contain '\0' we write the buffer, else the length of the string + '\0'
	if(value_length >= sizeof(value.value)){
		width += value_length;
	}else{
		width += value_length +1;
	}

	return width;
}

size_t ocpp_sampled_list_get_width(struct ocpp_sampled_value_list * list){
	size_t length = 0;

	while(list != NULL){
		if(list->value != NULL){
			length += ocpp_sample_get_width(*list->value);
		}
		list = list->next;
	}
	return length;
}

static size_t ocpp_sample_list_to_contiguous_buffer(struct ocpp_sampled_value_list * list, unsigned char * buffer, size_t buffer_size){
	size_t offset = 0;
	while(list != NULL){
		if(list->value != NULL){
			struct ocpp_sampled_value * current_value = list->value;

			if(buffer_size - offset < ocpp_sample_get_width(*current_value)){
				ESP_LOGE(TAG, "Insufficient buffer for sample");
				return -1;
			}

			size_t value_size = strnlen(list->value->value, sizeof(current_value->value));
			if(value_size < sizeof(current_value->value))
				value_size++;

			memcpy(buffer + offset, current_value->value, value_size);
			offset += value_size;

			size_t meta_width = sizeof(current_value->context) + sizeof(current_value->format)
				+ sizeof(current_value->measurand) + sizeof(current_value->phase)
				+ sizeof(current_value->location) + sizeof(current_value->unit);

			if(buffer_size - offset < meta_width){
				ESP_LOGE(TAG, "Insufficient buffer for sample meta data");
			}

			memcpy(buffer + offset, &current_value->context, sizeof(current_value->context));
			offset += sizeof(current_value->context);
			memcpy(buffer + offset, &current_value->format, sizeof(current_value->format));
			offset += sizeof(current_value->format);
			memcpy(buffer + offset, &current_value->measurand, sizeof(current_value->measurand));
			offset += sizeof(current_value->measurand);
			memcpy(buffer + offset, &current_value->phase, sizeof(current_value->phase));
			offset += sizeof(current_value->phase);
			memcpy(buffer + offset, &current_value->location, sizeof(current_value->location));
			offset += sizeof(current_value->location);
			memcpy(buffer + offset, &current_value->unit, sizeof(current_value->unit));
			offset += sizeof(current_value->unit);
		}
		list = list->next;
	}

	return offset;
}

#define MAX_BUFFER_LENGTH 16384
unsigned char * ocpp_meter_list_to_contiguous_buffer(struct ocpp_meter_value_list * list, bool is_stop_txn_data, size_t * buffer_length_out){
	struct ocpp_meter_value_list * list_start  = list;
	size_t length = 1;

	while(list != NULL){
		if(list->value != NULL && list->value->sampled_value != NULL){
			length += sizeof(time_t);
			length += ocpp_sampled_list_get_width(list->value->sampled_value);
			length += 1; // for ';'
		}

		list = list->next;
	}

	list = list_start;

	if(length > MAX_BUFFER_LENGTH){
		ESP_LOGE(TAG, "Unable to create create contiguous meter list buffer: Too long: %d chars", length);
		return NULL;
	}

	unsigned char * buffer = malloc(length);
	if(buffer == NULL){
		ESP_LOGE(TAG, "Unable to allocate contiguous buffer");
		return NULL;
	}

	size_t index = 0;
	size_t remaining_size = length;

	if(remaining_size >= sizeof(bool)){
		memcpy(buffer, &is_stop_txn_data, sizeof(bool));
		remaining_size -= sizeof(bool);
		index += sizeof(bool);

	}else{
		ESP_LOGE(TAG, "No buffer for contiguous buffer");
		free(buffer);
		return NULL;
	}

	while(list != NULL){
		if(list->value != NULL && list->value->sampled_value != NULL){
			if(remaining_size < sizeof(time_t)){
				ESP_LOGE(TAG, "Remaining contiguous buffer is insufficient for timestamp");
				free(buffer);
				return NULL;
			}

			memcpy(buffer + index, &list->value->timestamp, sizeof(time_t));
			remaining_size -= sizeof(time_t);
			index += sizeof(time_t);

			size_t written_length = ocpp_sample_list_to_contiguous_buffer(list->value->sampled_value, buffer + index, remaining_size);
			if(written_length == -1){
				ESP_LOGE(TAG, "Unable to write sample to contiguous buffer");
				free(buffer);
				return NULL;
			}else{
				remaining_size -= written_length;
				index += written_length;
			}

			if(remaining_size > 0){
				buffer[index++] = ';';
				remaining_size--;
			}else{
				free(buffer);
				return NULL;
			}
		}
		list = list->next;
	}

	if(remaining_size != 0){
		ESP_LOGE(TAG, "Expected to have written complete buffer. %d remaining", remaining_size);
		free(buffer);
		return NULL;
	}

	*buffer_length_out = length;
	return buffer;
}

struct ocpp_meter_value_list * ocpp_meter_list_from_contiguous_buffer(const unsigned char * buffer, size_t buffer_length, bool * is_stop_txn_data_out){

	struct ocpp_meter_value_list * result = ocpp_create_meter_list();
	if(result == NULL){
		ESP_LOGE(TAG, "Unable to create meter_value_list for contiguous buffer");
		return NULL;
	}

	struct ocpp_meter_value * meter_value = NULL;
	struct ocpp_sampled_value sample = {0};

	size_t offset = 0;
	size_t meta_width = sizeof(sample.context) + sizeof(sample.format) + sizeof(sample.measurand)
		+ sizeof(sample.phase) + sizeof(sample.location) + sizeof(sample.unit);

	if(buffer_length > sizeof(bool)){
		memcpy(is_stop_txn_data_out, buffer, sizeof(bool));
		offset += sizeof(bool);
	}else{
		ESP_LOGE(TAG, "Insufficient buffer length for meter value type");
		goto error;
	}

	while(offset < buffer_length){

		if(buffer_length < offset + sizeof(time_t)){
			ESP_LOGE(TAG, "Meter list is not at end, but has no space for timestamp");
			goto error;
		}

		meter_value = malloc(sizeof(struct ocpp_meter_value));
		if(meter_value == NULL){
			ESP_LOGE(TAG, "Unable to create new meter_value for short string");
			goto error;
		}

		memcpy(&meter_value->timestamp, buffer + offset, sizeof(time_t));
		offset += sizeof(time_t);

		meter_value->sampled_value = ocpp_create_sampled_list();
		if(meter_value->sampled_value == NULL){
			ESP_LOGE(TAG, "Unable to create sampled_value_list for short string");
			goto error;
		}

		while(*(buffer + offset) != ';' && offset + meta_width < buffer_length){
			size_t max_value_length;
			if((buffer_length - offset) > sizeof(sample.value)){
				max_value_length = sizeof(sample.value);
			}else{
				max_value_length = buffer_length - offset;
			}

			strncpy(sample.value, (char *)buffer+offset, max_value_length);
			size_t value_length = strnlen(sample.value, sizeof(sample.value));

			if(value_length >= sizeof(sample.value)){
				ESP_LOGE(TAG, "No NUL character written to meter value.");
				sample.value[sizeof(sample.value)-1] = '\0';
				offset += value_length;
			}else{
				offset += value_length +1;
			}

			if(offset + meta_width >= buffer_length){
				ESP_LOGE(TAG, "Value read from contiguous buffer but missing its meta data");
				goto error;
			}

			memcpy(&sample.context, buffer + offset, sizeof(sample.context));
			offset += sizeof(sample.context);
			memcpy(&sample.format, buffer + offset, sizeof(sample.format));
			offset += sizeof(sample.format);
			memcpy(&sample.measurand, buffer + offset, sizeof(sample.measurand));
			offset += sizeof(sample.measurand);
			memcpy(&sample.phase, buffer + offset, sizeof(sample.phase));
			offset += sizeof(sample.phase);
			memcpy(&sample.location, buffer + offset, sizeof(sample.location));
			offset += sizeof(sample.location);
			memcpy(&sample.unit, buffer + offset, sizeof(sample.unit));
			offset += sizeof(sample.unit);

			ocpp_sampled_list_add(meter_value->sampled_value, sample);
		}
		offset++;

		if(ocpp_meter_list_add_reference(result, meter_value) == NULL){
			ESP_LOGE(TAG, "Unable to add meter value to meter_value_list");
			goto error;
		}
		meter_value = NULL;
	}

	if(offset != buffer_length){
		ESP_LOGE(TAG, "Length of contiguous buffer read, but end not reached");
		goto error;
	}

	return result;

error:
	ocpp_meter_list_delete(result);
	if(meter_value != NULL){
		ocpp_sampled_list_delete(meter_value->sampled_value);
		free(meter_value);
	}

	return NULL;
}
