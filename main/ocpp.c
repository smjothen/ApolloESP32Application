#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"

#include "ocpp.h"
#include "connectivity.h"
#include "i2cDevices.h"
#include "storage.h"
#include "sessionHandler.h"

#include "ocpp_listener.h"
#include "ocpp_task.h"
#include "fat.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "ocpp_json/ocppj_message_structure.h"
#include "types/ocpp_reset_status.h"
#include "types/ocpp_reset_type.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_key_value.h"
#include "types/ocpp_configuration_status.h"
#include "types/ocpp_authorization_data.h"
#include "types/ocpp_authorization_status.h"
#include "types/ocpp_update_type.h"
#include "types/ocpp_update_status.h"
#include "types/ocpp_date_time.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_data_transfer_status.h"

#include "protocol_task.h"

#define TASK_OCPP_STACK_SIZE 2500
#define OCPP_PROBLEM_RESET_INTERVAL 30
#define OCPP_PROBLEMS_COUNT_BEFORE_RETRY 50
#define OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT 18000 // 5 hours
static const char * TAG = "OCPP";
static TaskHandle_t task_ocpp_handle = NULL;
StaticTask_t task_ocpp_buffer;
StackType_t task_ocpp_stack[TASK_OCPP_STACK_SIZE];

enum central_system_connection_status{
	eCS_CONNECTION_OFFLINE = 0,
	eCS_CONNECTION_ONLINE,
};

enum central_system_connection_status connection_status = eCS_CONNECTION_OFFLINE;

void not_supported_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	const char * description = (const char *)cb_data;

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_NOT_SUPPORTED, description, NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create response for missing action");
		return;
	}
	send_call_reply(ocpp_error);
	cJSON_Delete(ocpp_error);
	return;
}

void reset_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGW(TAG, "Received reset request");
	if(cJSON_HasObjectItem(payload, "type")){
		char * reset_type = cJSON_GetObjectItem(payload, "type")->valuestring;

		if(strcmp(reset_type, OCPP_RESET_TYPE_SOFT) == 0){
			// TODO: add support for soft restart
			cJSON * conf = ocpp_create_reset_confirmation(unique_id, OCPP_RESET_STATUS_REJECTED);
			if(conf == NULL){
				ESP_LOGE(TAG, "Unable to send reset rejected");
			}
			else{
				send_call_reply(conf);
				cJSON_Delete(conf);
			}
			return;
		}
		else if(strcmp(reset_type, OCPP_RESET_TYPE_HARD) == 0){
			// TODO: "If possible the Charge Point sends a StopTransaction.req for previously ongoing
			// transactions after having restarted and having been accepted by the Central System "
			cJSON * conf = ocpp_create_reset_confirmation(unique_id, OCPP_RESET_STATUS_ACCEPTED);
			if(conf == NULL){
				ESP_LOGE(TAG, "Unable to create reset confirmation");
			}
			else{
				send_call_reply(conf);
				cJSON_Delete(conf);
			}
			//TODO: Write restart reason
			ESP_LOGW(TAG, "Restarting esp");
			esp_restart(); //TODO: check if esp_restart is valid for ocpp Hard reset.
		}
		else{
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "'type' field does not conform to 'ResetType'", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
				return;
			}
			else{
				send_call_reply(ocpp_error);
				cJSON_Delete(ocpp_error);
				return;
			}
		}
	}else{
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "'type' field is required", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}
		else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
			return;
		}
	}
}


static int populate_sample_current_import(const char * context, struct ocpp_sampled_value_list * value_list_out){
	//Because the go only has 1 connector, we can get the current in the same way regardless of connector id

	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_CURRENT_IMPORT,
		.phase = OCPP_PHASE_L1,
		.location = OCPP_LOCATION_OUTLET,
		.unit = OCPP_UNIT_OF_MEASURE_A
	};
	strcpy(new_value.context, context);

	//Phase 1
	sprintf(new_value.value, "%f", MCU_GetCurrents(0));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	//Phase 2
	strcpy(new_value.phase, OCPP_PHASE_L2);
	sprintf(new_value.value, "%f", MCU_GetCurrents(1));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 1;

	//Phase 3
	strcpy(new_value.phase, OCPP_PHASE_L3);
	sprintf(new_value.value, "%f", MCU_GetCurrents(2));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 2;

	return 3;
}

//TODO: consider changing from using standalone current and if value should be changed as offered changes and prsence of car
static int populate_sample_current_offered(const char * context, struct ocpp_sampled_value_list * value_list_out){

	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_CURRENT_OFFERED,
		.unit = OCPP_UNIT_OF_MEASURE_A
	};
	strcpy(new_value.context, context);

	sprintf(new_value.value, "%f", MCU_StandAloneCurrent());
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static float last_aligned_energy_active_import_interval = 0;
static float last_sampled_energy_active_import_interval = 0;

static int populate_sample_energy_active_import_interval(const char * context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
		.unit = OCPP_UNIT_OF_MEASURE_WH
	};
	strcpy(new_value.context, context);

	if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_CLOCK) == 0){
		sprintf(new_value.value, "%f", MCU_GetEnergy() - last_aligned_energy_active_import_interval);
	}
	else if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_PERIODIC) == 0
		|| strcmp(context, OCPP_READING_CONTEXT_TRANSACTION_END) == 0){
		sprintf(new_value.value, "%f", MCU_GetEnergy() - last_sampled_energy_active_import_interval);
	}

	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static float populate_sample_power_active_import(const char * context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
		.location = OCPP_LOCATION_OUTLET,
		.unit = OCPP_UNIT_OF_MEASURE_W
	};
	strcpy(new_value.context, context);

	sprintf(new_value.value, "%f", MCU_GetPower());
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static int populate_sample_temperature(uint connector_id, const char * context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_TEMPERATURE,
		.location = OCPP_LOCATION_BODY,
		.unit = OCPP_UNIT_OF_MEASURE_CELSIUS
	};
	strcpy(new_value.context, context);

	if(connector_id == 0){
		// Body
		sprintf(new_value.value, "%f", MCU_GetTemperaturePowerBoard(0));
		if(ocpp_sampled_list_add(value_list_out, new_value) == NULL){
			return 0;
		}

		sprintf(new_value.value, "%f", MCU_GetTemperaturePowerBoard(1));
		if(ocpp_sampled_list_add(value_list_out, new_value) == NULL){
			return 1;
		}

		return 2;
	}else if(connector_id == 1){
		strcpy(new_value.location, OCPP_LOCATION_OUTLET);

		//phase 1
		strcpy(new_value.phase, OCPP_PHASE_L1);
		sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(0));
		if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
			return 0;

		//phase 2
		strcpy(new_value.phase, OCPP_PHASE_L2);
		sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(1));
		if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
			return 1;

		//phase 3
		strcpy(new_value.phase, OCPP_PHASE_L3);
		sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(2));
		if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
			return 2;

		return 3;
	}else{
		ESP_LOGE(TAG, "Unexpected connector id");
		return 0;
	}
}

static int populate_sample_voltage(const char * context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.format = OCPP_VALUE_FORMAT_RAW,
		.measurand = OCPP_MEASURAND_VOLTAGE,
		.phase = OCPP_PHASE_L1,
		.location = OCPP_LOCATION_OUTLET,
		.unit = OCPP_UNIT_OF_MEASURE_A
	};
	strcpy(new_value.context, context);

	//Phase 1
	sprintf(new_value.value, "%f", MCU_GetCurrents(0));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	//Phase 2
	strcpy(new_value.phase, OCPP_PHASE_L2);
	sprintf(new_value.value, "%f", MCU_GetCurrents(1));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 1;

	//Phase 3
	strcpy(new_value.phase, OCPP_PHASE_L3);
	sprintf(new_value.value, "%f", MCU_GetCurrents(2));
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 2;

	return 3;
}

void save_interval_measurands(const char * context){
	if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_CLOCK) == 0){
		last_aligned_energy_active_import_interval = MCU_GetEnergy();

	}else if(strcmp(context, OCPP_READING_CONTEXT_SAMPLE_PERIODIC) == 0
		|| strcmp(context, OCPP_READING_CONTEXT_TRANSACTION_BEGIN) == 0
		|| strcmp(context, OCPP_READING_CONTEXT_TRANSACTION_END) == 0){

		last_sampled_energy_active_import_interval = MCU_GetEnergy();
	}

}

// TODO: consider adding OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER
int populate_sample(const char * measurand, uint connector_id, const char * context, struct ocpp_sampled_value_list * value_list_out){
        if(strcmp(measurand, OCPP_MEASURAND_CURRENT_IMPORT) == 0){
		return populate_sample_current_import(context, value_list_out);

	}else if(strcmp(measurand, OCPP_MEASURAND_CURRENT_OFFERED) == 0){
		return populate_sample_current_offered(context, value_list_out);

	}else if(strcmp(measurand, OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL) == 0){
		return populate_sample_energy_active_import_interval(context, value_list_out);

	}else if(strcmp(measurand, OCPP_MEASURAND_POWER_ACTIVE_IMPORT) == 0){
		return populate_sample_power_active_import(context, value_list_out);

	}else if(strcmp(measurand, OCPP_MEASURAND_TEMPERATURE) == 0){
		return populate_sample_temperature(connector_id, context, value_list_out);

	}else if(strcmp(measurand, OCPP_MEASURAND_VOLTAGE) == 0){
		return populate_sample_voltage(context, value_list_out);

	}else{
		ESP_LOGE(TAG, "Invalid measurand '%s'!!", measurand);
		return 0;
	}
}

//TODO: "All "per-period" data [...] should be [...] transmitted [...] at the end of each interval, bearing the interval start time timestamp"
//TODO: Optionally add ISO8601 duration to meter value timestamp
int ocpp_populate_meter_values(uint connector_id, const char * context,
			const char * measurand_csl, struct ocpp_meter_value * meter_value_out){

	if(strlen(measurand_csl) == 0){
		return 0;
	}

	/**
	 * The amount of samples to add to any ocpp meter value may depend on:
	 * how many measurands in the measurand_csl (comma seperated list),
	 * how many measurands are supported on the current connector_id,
	 * how many locations a measurand can be measured,
	 * and how many phases should be measured
	 */

	char * measurands = strdup(measurand_csl);
	if(measurands == NULL){
		ESP_LOGE(TAG, "Unable to duplicate measurands");
		return -1;
	}


	struct ocpp_sampled_value_list * last_ptr = &meter_value_out->sampled_value;
	char * item = strtok(measurands, ",");

	while(item != NULL){
		ESP_LOGI(TAG, "Attempting to add %s to meter value", item);

		int new_item_count = 0;

		new_item_count = populate_sample(item, connector_id, context, last_ptr);

		if(new_item_count < 0){
			ESP_LOGE(TAG, "Failed to populate sample for measurand '%s'", item);
		}else{
			last_ptr = ocpp_sampled_list_get_last(last_ptr);
		}

		item = strtok(NULL, ",");
	}

	free(measurands);

	return ocpp_sampled_list_get_length(&meter_value_out->sampled_value);
}

int ocpp_populate_meter_values_from_existing(uint connector_id, const char * context, const char * measurand_csl,
					struct ocpp_sampled_value_list existing_list, struct ocpp_meter_value * meter_value_out){

	struct ocpp_sampled_value_list * last_ptr = &meter_value_out->sampled_value;
	char * measurands = strdup(measurand_csl);
	if(measurands == NULL){
		ESP_LOGE(TAG, "Unable to duplicate measurands");
		return -1;
	}

	char * item = strtok(measurands, ",");

	while(item != NULL){
		bool added = false;
		struct ocpp_sampled_value_list * existing_value = &existing_list;

		while(existing_value != NULL){
			if(strcmp(item, existing_value->value->measurand) == 0){
				if(ocpp_sampled_list_add(last_ptr, *existing_value->value) == NULL){
					ESP_LOGE(TAG, "Unable to add exiting measurand");
				}

				added = true;
			}
		}

		if(added == false){
			int new_item_count = populate_sample(item, connector_id, context, last_ptr);

			if(new_item_count < 0){
				ESP_LOGE(TAG, "Failed to populate sample for measurand '%s'", item);
			}else if(new_item_count > 0){
				added = true;
			}
		}

		if(added)
			last_ptr = ocpp_sampled_list_get_last(last_ptr);

		item = strtok(NULL, ",");
	}

	free(measurands);

	return ocpp_sampled_list_get_length(&meter_value_out->sampled_value);
}


TimerHandle_t clock_aligned_handle = NULL;

static void meter_values_response_cb(){
	ESP_LOGI(TAG, "Meter values complete");
}

static void meter_values_error_cb(){
	ESP_LOGE(TAG, "Meter values completed with errors");
}

static void clock_aligned_meter_values(){
	ESP_LOGI(TAG, "Starting clock aligned meter values");

	for(size_t i = 0; i <= storage_Get_ocpp_number_of_connectors(); i++){
		ESP_LOGI(TAG, "Creating meter values for connector %d", i);
		struct ocpp_meter_value meter_value = {0};
		meter_value.timestamp = time(NULL);

		int length = ocpp_populate_meter_values(i, OCPP_READING_CONTEXT_SAMPLE_CLOCK, storage_Get_ocpp_meter_values_aligned_data(), &meter_value);
		if(length < 1){
			ESP_LOGW(TAG, "No elements in meter value list. Cancel sending of meter value");
			continue; // TODO: do not skip stoptxn
		}

		// TODO: consider splitting some of the SampledValues into separate MeterValues
		cJSON * request = ocpp_create_meter_values_request(i, NULL, 1, &meter_value);
		if(request == NULL){
			ESP_LOGE(TAG, "Unable to create meter value request for clock aligned meter values");
			return;
		}

		ESP_LOGW(TAG, "Sending meter values");
		if(enqueue_call(request, meter_values_response_cb, meter_values_error_cb, NULL, eOCPP_CALL_GENERIC) != 0){
			ESP_LOGE(TAG, "Unable to send meter values");
			cJSON_Delete(request);
		}

		if(sessionHandler_OcppTransactionIsActive()){

			struct ocpp_meter_value stoptxn_meter_value = {0};
			int length = ocpp_populate_meter_values_from_existing(i, OCPP_READING_CONTEXT_SAMPLE_CLOCK, storage_Get_ocpp_stop_txn_aligned_data(),
								meter_value.sampled_value, &stoptxn_meter_value);

			if(length > 0){
				//TODO: Inform session of new clock aligned value
			}
		}
		ocpp_sampled_list_delete(meter_value.sampled_value);
	}

	// Interval measurands are saved regardless of related CSL in case the CSL configuration is changed
	save_interval_measurands(OCPP_READING_CONTEXT_SAMPLE_CLOCK);

}

static void clock_aligned_meter_values_on_aligned_start(){
	ESP_LOGI(TAG, "Aligned for meter values, sending values and starting repeating timer");

	clock_aligned_meter_values();
	clock_aligned_handle = xTimerCreate("Ocpp clock aligned",
					pdMS_TO_TICKS(storage_Get_ocpp_clock_aligned_data_interval() * 1000),
					pdTRUE, NULL, clock_aligned_meter_values);

	if(clock_aligned_handle == NULL){
		ESP_LOGE(TAG, "Unable to create repeating clock aligned meter values timer");

	}else{
		xTimerStart(clock_aligned_handle, pdMS_TO_TICKS(200));
	}
}

static void restart_clock_aligned_meter_values(){
	ESP_LOGI(TAG, "Restarting clock aligned meter value timer");

	if(clock_aligned_handle != NULL){
		xTimerDelete(clock_aligned_handle, pdMS_TO_TICKS(200));
		clock_aligned_handle = NULL;
	}

	uint32_t interval = storage_Get_ocpp_clock_aligned_data_interval();

	if(interval != 0){
		time_t current_time = time(NULL);
		struct tm * t = localtime(&current_time);

		// offset from 00:00:00 (midnight)
		uint offset = t->tm_sec + t->tm_min * 60 + t->tm_hour * 60 * 60;

		// offset to next clock aligned meter value
		offset = interval - (offset % interval);

		if(offset > 0){ // If we are not already aligned, wait for alignment
			ESP_LOGI(TAG, "Meter value alignment in %d seconds", offset);

			clock_aligned_handle = xTimerCreate("Ocpp clock aligned first",
							pdMS_TO_TICKS(offset * 1000),
							pdFALSE, NULL, clock_aligned_meter_values_on_aligned_start);
			if(clock_aligned_handle == NULL){
				ESP_LOGE(TAG, "Unable to create clock aligned meter values timer");

			}else{
				xTimerStart(clock_aligned_handle, pdMS_TO_TICKS(200));
			}

		}else{ // If we are aligned, we start the repeating timer imediatly
			clock_aligned_meter_values_on_aligned_start();
		}
	}else{
		ESP_LOGW(TAG, "Clock aligned meter values are disabled");
	}
}

static int allocate_and_write_configuration_u8(uint8_t value, char ** value_out){
	*value_out = malloc(sizeof(char) * 4);
	if(*value_out == NULL){
		return -1;
	}else{
		snprintf(*value_out, 4, "%d", value);
		return 0;
	}
}

static int allocate_and_write_configuration_u16(uint16_t value, char ** value_out){
	*value_out = malloc(sizeof(char) * 8);
	if(*value_out == NULL){
		return -1;
	}else{
		snprintf(*value_out, 8, "%d", value);
		return 0;
	}
}

static int allocate_and_write_configuration_u32(uint32_t value, char ** value_out){
	*value_out = malloc(sizeof(char) * 16);
	if(*value_out == NULL){
		return -1;
	}else{
		snprintf(*value_out, 16, "%d", value);
		return 0;
	}
}

int allocate_and_write_configuration_bool(bool value, char ** value_out){
	*value_out = malloc(sizeof(char) * 8);
	if(*value_out == NULL){
		return -1;
	}else{
		strcpy(*value_out, value ? "true" : "false");
		return 0;
	}
}

static int allocate_and_write_configuration_str(const char * value, char ** value_out){
	size_t length = strlen(value);
	if(length > 500)
		return -1;

	*value_out = malloc((length + 1) * sizeof(char));
	if(*value_out == NULL){
		return -1;
	}else{
		strcpy(*value_out, value);
		return 0;
	}
}

static void free_configuration_key(struct ocpp_key_value * configuration_key, size_t key_count){
	if(configuration_key == NULL)
		return;

	for(size_t i = 0; i < key_count; i++){
		free(configuration_key[i].value);
	}
	free(configuration_key);
}

static void free_unknown_key(char ** unknown_key, size_t key_count){
	if(unknown_key == NULL)
		return;

	for(size_t i = 0; i < key_count; i++){
		free(unknown_key[i]);
	}
	free(unknown_key);
}

static char * convert_to_ocpp_phase(uint8_t phase_rotation){
	switch(phase_rotation){ //TODO: Check if understood correctly and handling of 1,2,3 and 1,11,12 is correct
	case 1:
	case 2:
	case 3:
		return "NotApplicable";
	case 4:
		return "RST";
	case 5:
		return "STR";
	case 6:
		return "TRS";
	case 7:
		return "RTS";
	case 8:
		return "SRT";
	case 9:
		return "TSR";
	case 10:
	case 11:
	case 12:
		return "NotApplicable";
	case 13:
		return "RST";
	case 14:
		return "STR";
	case 15:
		return "TRS";
	case 16:
		return "RTS";
	case 17:
		return "SRT";
	case 18:
		return "TSR";
	default:
		return "Unknown";
	}
}

// Returns 0 if it can not be determined
static uint8_t convert_from_ocpp_phase(const char * phase_rotation, bool is_it){
	if(!is_it){
		if(strcmp(phase_rotation, "RST") == 0){
			return 4;
		}else if(strcmp(phase_rotation, "STR") == 0){
			return 5;
		}else if(strcmp(phase_rotation, "TRS") == 0){
			return 6;
		}else if(strcmp(phase_rotation, "RTS") == 0){
			return 7;
		}else if(strcmp(phase_rotation, "SRT") == 0){
			return 8;
		}else if(strcmp(phase_rotation, "TSR") == 0){
			return 9;
		}else{
			return 0;
		}
	}
	else{
		if(strcmp(phase_rotation, "RST") == 0){
			return 13;
		}else if(strcmp(phase_rotation, "STR") == 0){
			return 14;
		}else if(strcmp(phase_rotation, "TRS") == 0){
			return 15;
		}else if(strcmp(phase_rotation, "RTS") == 0){
			return 16;
		}else if(strcmp(phase_rotation, "SRT") == 0){
			return 17;
		}else if(strcmp(phase_rotation, "TSR") == 0){
			return 18;
		}else{
			return 0;
		}

	}
}
static int get_ocpp_configuration(const char * key, struct ocpp_key_value * configuration_out){
	strcpy(configuration_out->key, key);

	if(strcmp(key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_authorize_remote_tx_requests(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u32(
			storage_Get_ocpp_clock_aligned_data_interval(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u32(
			storage_Get_ocpp_connection_timeout(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION) == 0){
 		configuration_out->readonly = false;

		char phase_rotation_str[16];
		sprintf(phase_rotation_str, "1.%s", convert_to_ocpp_phase(storage_Get_PhaseRotation()));

		return allocate_and_write_configuration_str(phase_rotation_str, &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_connector_phase_rotation_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_get_configuration_max_keys(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u32(
			storage_Get_ocpp_heartbeat_interval(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LIGHT_INTENSITY) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u8(
			floor(storage_Get_HmiBrightness() * 100), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_local_authorize_offline(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_local_pre_authorize(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_str(
			storage_Get_ocpp_meter_values_aligned_data(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_meter_values_aligned_data_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_str(
			storage_Get_ocpp_meter_values_sampled_data(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_meter_values_sampled_data_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u32(
			storage_Get_ocpp_meter_value_sample_interval(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_number_of_connectors(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_RESET_RETRIES) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_reset_retries(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_stop_transaction_on_ev_side_disconnect(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_stop_transaction_on_invalid_id(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_str(
			storage_Get_ocpp_stop_txn_aligned_data(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_stop_txn_aligned_data_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_str(
			storage_Get_ocpp_stop_txn_sampled_data(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_stop_txn_sampled_data_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_str(
			storage_Get_ocpp_supported_feature_profiles(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_supported_feature_profiles_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_transaction_message_attempts(), &configuration_out->value);


	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_u16(
			storage_Get_ocpp_transaction_message_retry_interval(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_unlock_connector_on_ev_side_disconnect(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED) == 0){
		configuration_out->readonly = false;

		return allocate_and_write_configuration_bool(
			storage_Get_ocpp_local_auth_list_enabled(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u16(
			storage_Get_ocpp_local_auth_list_max_length(), &configuration_out->value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH) == 0){
		configuration_out->readonly = true;

		return allocate_and_write_configuration_u8(
			storage_Get_ocpp_send_local_list_max_length(), &configuration_out->value);

	}else{
		configuration_out->readonly = true;
		configuration_out->value = malloc(sizeof(char) * 30);
		if(configuration_out->value == NULL){
			return -1;
		}else{
			strcpy(configuration_out->value, "UNHANDLED_CONFIGURATION_ERROR");
			return 0;
		}
	}
}

static int get_all_ocpp_configurations(struct ocpp_key_value * configuration_out){
	size_t index = 0;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS);
	configuration_out[index].readonly = false;
	int err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_authorize_remote_tx_requests(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;
	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u32(
		storage_Get_ocpp_clock_aligned_data_interval(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u32(
		storage_Get_ocpp_connection_timeout(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION);
	configuration_out->readonly = false;

	char phase_rotation_str[16];
	sprintf(phase_rotation_str, "1.%s", convert_to_ocpp_phase(storage_Get_PhaseRotation()));

	err = allocate_and_write_configuration_str(phase_rotation_str, &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_connector_phase_rotation_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_get_configuration_max_keys(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u32(
		storage_Get_ocpp_heartbeat_interval(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_LIGHT_INTENSITY);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u8(
		floor(storage_Get_HmiBrightness() * 100), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_local_authorize_offline(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_local_pre_authorize(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_str(
			storage_Get_ocpp_meter_values_aligned_data(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_meter_values_aligned_data_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_str(
		storage_Get_ocpp_meter_values_sampled_data(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_meter_values_sampled_data_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u32(
		storage_Get_ocpp_meter_value_sample_interval(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_number_of_connectors(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_RESET_RETRIES);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_reset_retries(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_stop_transaction_on_ev_side_disconnect(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_stop_transaction_on_invalid_id(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_str(
		storage_Get_ocpp_stop_txn_aligned_data(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_stop_txn_aligned_data_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_str(
		storage_Get_ocpp_stop_txn_sampled_data(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_stop_txn_sampled_data_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_str(
		storage_Get_ocpp_supported_feature_profiles(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_supported_feature_profiles_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_transaction_message_attempts(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;


	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_transaction_message_retry_interval(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_unlock_connector_on_ev_side_disconnect(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED);
	configuration_out->readonly = false;

	err = allocate_and_write_configuration_bool(
		storage_Get_ocpp_local_auth_list_enabled(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u16(
		storage_Get_ocpp_local_auth_list_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;
	index++;

	strcpy(configuration_out[index].key, OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH);
	configuration_out->readonly = true;

	err = allocate_and_write_configuration_u8(
		storage_Get_ocpp_send_local_list_max_length(), &configuration_out[index].value);
	if(err != 0)
		goto error;

	return 0;

error:
	free_configuration_key(configuration_out, index);
	return -1;
}


//TODO: check if this should be scheduled for different thread to free up the websocket thread
static void get_configuration_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	if(cJSON_HasObjectItem(payload, "key")){
		cJSON * key = cJSON_GetObjectItem(payload, "key");

		if(!cJSON_IsArray(key)){
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "'key' field expected to be array type", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for type constraint violation of 'key' field");
				return;
			}
			else{
				send_call_reply(ocpp_error);
				cJSON_Delete(ocpp_error);
				return;
			}
		}

		int key_length = cJSON_GetArraySize(key);
		if(key_length > storage_Get_ocpp_get_configuration_max_keys()){
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "Too many keys requested", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for property constraint violation");
				return;
			}else{
				send_call_reply(ocpp_error);
				cJSON_Delete(ocpp_error);
				return;
			}
		}

		size_t configuration_key_index = 0; // for keys that are recognized/implemented
		size_t unknown_key_index = 0;

		// Validate 'key' items and determine how many keys need to be allocated
		for(size_t i = 0; i < key_length; i++){
			cJSON * key_id = cJSON_GetArrayItem(key, i);
			if(!cJSON_IsString(key_id) || !is_ci_string_type(key_id->valuestring, 50)){
				cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected key to contain items of CiString50Type", NULL);
				if(ocpp_error == NULL){
					ESP_LOGE(TAG, "Unable to create call error for type constraint violation of 'key' field inner items");
					return;
				}else{
					send_call_reply(ocpp_error);
					cJSON_Delete(ocpp_error);
					return;
				}
			}

			if(is_configuration_key(key_id->valuestring)){
				configuration_key_index++;
			}else{
				unknown_key_index++;
			}
		}

		struct ocpp_key_value * configuration_key_buffer = NULL;
		char ** unknown_key_buffer = NULL;

		if(configuration_key_index > 0){
			configuration_key_buffer = malloc(configuration_key_index * sizeof(struct ocpp_key_value));
			if(configuration_key_buffer == NULL){
				goto error;
			}
		}

		if(unknown_key_index > 0){
			unknown_key_buffer = malloc(unknown_key_index * sizeof(char*));
			if(unknown_key_buffer == NULL){
				free(configuration_key_buffer);
				goto error;
			}
			for(size_t i = 0; i < unknown_key_index; i++){
				unknown_key_buffer[i] = malloc(51 * sizeof(char));
				if(unknown_key_buffer[i] == NULL){
					while(i > 0){
						free(unknown_key_buffer[--i]);
					}

					free(unknown_key_buffer);
					free(configuration_key_buffer);

					goto error;
				}
			}
		}

		configuration_key_index = 0;
		unknown_key_index = 0;

		for(size_t i = 0; i < key_length; i++){
			const char * key_str = cJSON_GetArrayItem(key, i)->valuestring;
			if(is_configuration_key(key_str)){
				int err = get_ocpp_configuration(key_str, &configuration_key_buffer[configuration_key_index++]);
				if(err != 0){
					ESP_LOGE(TAG, "Error while getting ocpp configuration, aborting get configuration cb");
					free_configuration_key(configuration_key_buffer, configuration_key_index-1);
					free_unknown_key(unknown_key_buffer, unknown_key_index);

					goto error;
				}
			}
			else{
				strcpy(unknown_key_buffer[unknown_key_index++], key_str);
			}
		}

		cJSON * response = ocpp_create_get_configuration_confirmation(unique_id, configuration_key_index, configuration_key_buffer, unknown_key_index, unknown_key_buffer);

		free_configuration_key(configuration_key_buffer, configuration_key_index);
		free_unknown_key(unknown_key_buffer, unknown_key_index);

		if(response == NULL){
			ESP_LOGE(TAG, "Unable to create configuration response");

			goto error;
		}else{
			send_call_reply(response);
			cJSON_Delete(response);

			return;
		}
	}else{ // No keys in request, send all
		struct ocpp_key_value * key_values = malloc(sizeof(struct ocpp_key_value) * OCPP_CONFIG_KEY_COUNT);
		if(key_values == NULL){
			goto error;
		}
		int err = get_all_ocpp_configurations(key_values);
		if(err != 0){
			ESP_LOGE(TAG, "Unable to get all configurations");
			goto error;
		}

		cJSON * response = ocpp_create_get_configuration_confirmation(unique_id, OCPP_CONFIG_KEY_COUNT, key_values, 0, NULL);

		free_configuration_key(key_values, OCPP_CONFIG_KEY_COUNT);

		if(response == NULL){
			ESP_LOGE(TAG, "Unable to create configuration response");
			goto error;
		}
		else{
			send_call_reply(response);
			cJSON_Delete(response);

			return;
		}
	}

error: ;

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "", NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create call error for internal error");
		return;
	}else{
		send_call_reply(ocpp_error);
		cJSON_Delete(ocpp_error);
		return;
	}
}

static void change_config_confirm(const char * unique_id, const char * configuration_status){
	cJSON * response = ocpp_create_change_configuration_confirmation(unique_id, configuration_status);
	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create change configuration confirmation");
		return;
	}else{
		send_call_reply(response);
		cJSON_Delete(response);
	}
}

static bool is_valid_alignment_interval(uint32_t sec){
	return (86400 % sec) == 0 ? true : false;
}

static long validate_u(const char * value, uint32_t upper_bounds){
	char * endptr;
	long value_long = strtol(value, &endptr, 0);

	if(endptr[0] != '\0'){
		ESP_LOGE(TAG, "Negative value");
		return -1;
	}

	if(value_long < 0 || value_long > upper_bounds){
		ESP_LOGE(TAG, "%ld Exceeds %d", value_long, upper_bounds);
		return -1;
	}

	return value_long;
}

static int set_config_u8(void (*config_function)(uint8_t), const char * value){
	long value_long = validate_u(value, UINT8_MAX);

	if(value_long == -1)
		return -1;

	config_function((uint8_t)value_long);
	return 0;
}

static int set_config_u16(void (*config_function)(uint16_t), const char * value){
	long value_long = validate_u(value, UINT16_MAX);

	if(value_long == -1)
		return -1;

	config_function((uint16_t)value_long);
	return 0;
}

static int set_config_u32(void (*config_function)(uint32_t), const char * value, bool (*additional_validation)(uint32_t)){
	long value_long = validate_u(value, UINT32_MAX);

	if(value_long == -1)
		return -1;

	if(additional_validation != NULL && additional_validation((uint32_t)value_long) == false)
		return -1;

	config_function((uint32_t)value_long);
	return 0;
}

static int set_config_bool(void (*config_function)(bool), const char * value){
	if(strcasecmp(value, "true") == 0){
		config_function(true);
	}else if(strcasecmp(value, "false") == 0){
		config_function(false);
	}else{
		return -1;
	}
	return 0;
}

static int set_config_csl(void (*config_function)(const char *), const char * value, uint8_t max_items, size_t option_count, ...){
	//Check if given any configurations
	size_t len = strlen(value);

	if(len == 0)
		return -1;

	//Remove whitespace and check for control chars
	char * value_prepared = malloc(len +1);
	char * config_str = NULL;

	size_t prepared_index = 0;
	for(size_t i = 0; i < len+1; i++){
		if(!isspace(value[i])){
			if(iscntrl(value[i])){
				if(value[i]== '\0'){
					value_prepared[prepared_index++] = value[i];
					break;
				}else{
					ESP_LOGW(TAG, "CSL contains unexpected control character");
					goto error; // Dont trust input with unexpected control characters
				}
			}
			value_prepared[prepared_index++] = value[i];
		}
	}

	//Check if given configuration was only space
	if(strlen(value_prepared) == 0){
		ESP_LOGW(TAG, "CSL contained no relevant data");
		goto error;
	}
	size_t item_count = 1;

	// Check if number of items exceed max
	char * delimiter_ptr = value_prepared;
	for(size_t i = 0; i < max_items + 1; i++){
		delimiter_ptr = strchr(delimiter_ptr, ',');

		if(delimiter_ptr == NULL){
			break;
		}else{
			delimiter_ptr++;
			item_count++;
		}
	}

	if(item_count > max_items){
		ESP_LOGW(TAG, "CSL item count exceed maximum number of values: %d/%d", item_count, max_items);
		goto error;
	}

	config_str = strdup(value_prepared);
	if(config_str == NULL){
		ESP_LOGE(TAG, "Unable to duplicat CSL");
		goto error;
	}

	// Check if each item is among options
	char * token = strtok(value_prepared, ",");
	while(token != NULL){
		va_list argument_ptr;
		bool is_valid = false;

		va_start(argument_ptr, option_count);
		for(int i = 0; i < option_count; i++){
			const char * enum_value = va_arg(argument_ptr, const char *);
			if(strcmp(token, enum_value) == 0){
				is_valid = true;
				break;
			}
		}
		va_end(argument_ptr);
		if(!is_valid){
			ESP_LOGW(TAG, "CSL contained invalid item: '%s'", token);
			goto error;
		}
		token = strtok(NULL, ",");
	}
	free(value_prepared);

	ESP_LOGI(TAG, "Writing CSL value: '%s'", config_str);
	config_function(config_str);
	free(config_str);

	return 0;

error:
	free(value_prepared);
	free(config_str);
	return -1;
}

static void change_configuration_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Recieved request to change configuration");
	if(!cJSON_HasObjectItem(payload, "key") || !cJSON_HasObjectItem(payload, "value")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'key' and 'value' fields", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
			return;
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
			return;
		}
	}

	cJSON * key_json = cJSON_GetObjectItem(payload, "key");
	cJSON * value_json = cJSON_GetObjectItem(payload, "value");

	if(!cJSON_IsString(key_json) || !cJSON_IsString(value_json) ||
		!is_ci_string_type(key_json->valuestring, 50) || !is_ci_string_type(value_json->valuestring, 500)){

		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'key' to be CiString50Type  and 'value' to be CiString500Type", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
			return;
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
			return;
		}

	}

	const char * key = key_json->valuestring;
	const char * value = value_json->valuestring;

	ESP_LOGI(TAG, "Given configuration: \n\tkey: '%s'\n\tvalue: '%s'", key, value);
	int err = -1;
	if(strcmp(key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS) == 0){
		err = set_config_bool(storage_Set_ocpp_authorize_remote_tx_requests, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_clock_aligned_data_interval, value, is_valid_alignment_interval);
		if(err == 0)
			restart_clock_aligned_meter_values();

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT) == 0){
		err = set_config_u32(storage_Set_ocpp_connection_timeout, value, NULL);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION) == 0){
		char * endptr;
		long connector_id = strtol(value, &endptr, 0);

		// if connector id is not present on cp or iondicate all connectors, or not followed by '.' and a valid phase_rotation
		if((connector_id <= storage_Get_ocpp_number_of_connectors() && connector_id != 0) || strlen(endptr) != 4){
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
			return;
		}

		//TODO: find better way to check if it should be TN or IT connector wiring
		uint8_t wire_index = convert_from_ocpp_phase(value, storage_Get_PhaseRotation() > 9);
		if(wire_index == 0){
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
			return;
		}

		storage_Set_PhaseRotation(wire_index);
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);

	}else if(strcmp(key, OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_heartbeat_interval, value, NULL);
		if(err == 0)
			update_heartbeat_timer(storage_Get_ocpp_heartbeat_interval());

	}else if(strcmp(key, OCPP_CONFIG_KEY_LIGHT_INTENSITY) == 0){
		char * endptr;
		long value_long = strtol(value, &endptr, 0);

		if(endptr[0] == '0' || value_long < 0 || value_long > 100){
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
			return;
		}

		storage_Set_HmiBrightness(value_long / 100.0f);
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);
		return;

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE) == 0){
		err = set_config_bool(storage_Set_ocpp_local_authorize_offline, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE) == 0){
		err = set_config_bool(storage_Set_ocpp_local_pre_authorize, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_meter_values_aligned_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

		// TODO: "where applicable, the Measurand is combined with the optional phase; for instance: Voltage.L1"
	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_meter_values_sampled_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_meter_value_sample_interval, value, NULL);

	}else if(strcmp(key, OCPP_CONFIG_KEY_RESET_RETRIES) == 0){
		err = set_config_u8(storage_Set_ocpp_reset_retries, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT) == 0){
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_ev_side_disconnect, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID) == 0){
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_invalid_id, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_aligned_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_sampled_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS) == 0){
		err = set_config_u8(storage_Set_ocpp_transaction_message_attempts, value);
		if(err == 0)
			update_transaction_message_related_config(
				storage_Get_ocpp_transaction_message_attempts(),
				storage_Get_ocpp_transaction_message_retry_interval());

	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL) == 0){
		err = set_config_u16(storage_Set_ocpp_transaction_message_retry_interval, value);
		if(err == 0)
			update_transaction_message_related_config(
				storage_Get_ocpp_transaction_message_attempts(),
				storage_Get_ocpp_transaction_message_retry_interval());

	}else if(strcmp(key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT) == 0){
		err = set_config_bool(storage_Set_ocpp_unlock_connector_on_ev_side_disconnect, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED) == 0){
		err = set_config_bool(storage_Set_ocpp_local_auth_list_enabled, value);

	}else if(is_configuration_key(key)){
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
		return;
	}else{
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_NOT_SUPPORTED);
		return;
	}

	if(err == 0){
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);
		storage_SaveConfiguration();
	}else{
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
	}

	return;
}

#define SEND_LOCAL_LIST_MAX_INNER_REASON_SIZE 128
#define SEND_LOCAL_LIST_MAX_REASON_SIZE 256

static int validate_and_convert_auth_data(cJSON * auth_data, bool is_update_type_full, struct ocpp_authorization_data * data_out, char * error_type_out, char * error_reason_out){

	if(!cJSON_HasObjectItem(auth_data, "idTag")){
		strcpy(error_type_out, OCPPJ_ERROR_FORMATION_VIOLATION);
		strcpy(error_reason_out, "Expected 'idTag' field");
		return -1;
	}

	if(is_update_type_full && !cJSON_HasObjectItem(auth_data, "idTagInfo")){
		strcpy(error_type_out, OCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION);
		strcpy(error_reason_out, "Expected 'idTagInfo' field when 'updateType' is Full");
		return -1;
	}

	cJSON * id_tag_json = cJSON_GetObjectItem(auth_data, "idTag");
	if(!cJSON_IsString(id_tag_json) || !is_ci_string_type(id_tag_json->valuestring, 20)){
		strcpy(error_type_out, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION);
		strcpy(error_reason_out, "Expected 'idTag' field to be of CiString20Type");
		return -1;
	}
	strcpy(data_out->id_tag, id_tag_json->valuestring);

	if(cJSON_HasObjectItem(auth_data, "idTagInfo")){
		cJSON * id_tag_info_json = cJSON_GetObjectItem(auth_data, "idTagInfo");
		if(cJSON_HasObjectItem(id_tag_info_json, "expiryDate")){
			data_out->id_tag_info.expiry_date = ocpp_parse_date_time(cJSON_GetObjectItem(id_tag_info_json, "expiryDate")->valuestring);

			if(data_out->id_tag_info.expiry_date == -1){
				strcpy(error_type_out, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION);
				strcpy(error_reason_out, "Unable to validate 'expiryDate'");
				return -1;
			}
		}else{
			data_out->id_tag_info.expiry_date = 0;
		}

		if(cJSON_HasObjectItem(id_tag_info_json, "parentIdTag")){
			cJSON * parent_id_json = cJSON_GetObjectItem(id_tag_info_json, "parentIdTag");

			if(!cJSON_IsString(parent_id_json) || !is_ci_string_type(parent_id_json->valuestring, 20)){
				strcpy(error_type_out, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION);
				strcpy(error_reason_out, "Expected 'parentIdTag' field to be of CiString20Type");
				return -1;
			}

			strcpy(data_out->id_tag_info.parent_id_tag, parent_id_json->valuestring);
		}else{
			strcpy(data_out->id_tag_info.parent_id_tag, "");
		}

		if(!cJSON_HasObjectItem(id_tag_info_json, "status")){
			strcpy(error_type_out, OCPPJ_ERROR_FORMATION_VIOLATION);
			strcpy(error_reason_out, "Expected 'status' field");
			return -1;
		}

		cJSON * status_json = cJSON_GetObjectItem(id_tag_info_json, "status");
		if(!cJSON_IsString(status_json) || ocpp_validate_enum(status_json->valuestring, 4,
									OCPP_AUTHORIZATION_STATUS_ACCEPTED,
									OCPP_AUTHORIZATION_STATUS_BLOCKED,
									OCPP_AUTHORIZATION_STATUS_EXPIRED,
									OCPP_AUTHORIZATION_STATUS_INVALID) != 0)
		{
			strcpy(error_type_out, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION);
			strcpy(error_reason_out, "Expected 'status' to be appropriate AuthorizationStatus type");
			return -1;
		}
		strcpy(data_out->id_tag_info.status, status_json->valuestring);

	}else{
		strcpy(data_out->id_tag_info.status, "DELETE");
	}

	return 0;
}

//Will delete items in auth_list to not use excessive memory
static int validate_and_convert_auth_list(cJSON * auth_list, bool is_update_type_full, struct ocpp_authorization_data ** data_out, char * error_type_out, char * error_reason_out){
 	if(auth_list == NULL)
		return 0;

	int tag_count = cJSON_GetArraySize(auth_list);
	if(tag_count > storage_Get_ocpp_send_local_list_max_length()){
		strcpy(error_type_out, OCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION);
		snprintf(error_reason_out, SEND_LOCAL_LIST_MAX_REASON_SIZE, "Number of elements in 'localAuthorizationList' exceed SendLocalListMaxLength");
		return -1;
	}

	char local_error_reason[SEND_LOCAL_LIST_MAX_INNER_REASON_SIZE];

	for(size_t i = 0; i < tag_count; i++){
		cJSON * auth_data = cJSON_DetachItemFromArray(auth_list, 0);
		struct ocpp_authorization_data * item_out = malloc(sizeof(struct ocpp_authorization_data));
		if(item_out == NULL){
			strcpy(error_type_out, OCPPJ_ERROR_INTERNAL);
			strcpy(error_reason_out, "Unable to allocate memory");
			return -1;
		}

		if(validate_and_convert_auth_data(auth_data, is_update_type_full, item_out, error_type_out, local_error_reason) != 0){
			cJSON_Delete(auth_data);

			for(size_t j = 0; j < i; j++)
				free(data_out[i]);

			free(item_out);

			snprintf(error_reason_out, SEND_LOCAL_LIST_MAX_REASON_SIZE, "While parsing element %d: %s", i, local_error_reason);
			return -1;
		}

		for(size_t j = 0; j < i; j++){
			if(strcmp(item_out->id_tag, data_out[j]->id_tag) == 0){
				strcpy(error_type_out, OCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION);
				snprintf(error_reason_out, SEND_LOCAL_LIST_MAX_REASON_SIZE, "Duplicate idTag not allowed: %s", item_out->id_tag);

				for(size_t k = 0; k < i; k++)
					free(data_out[k]);

				free(item_out);

				return -1;
			}
		}

		data_out[i] = item_out;
		cJSON_Delete(auth_data);
	}
	return tag_count;
}

void free_auth_list(struct ocpp_authorization_data ** auth_list, int auth_list_length){
	for(size_t i = 0; i < auth_list_length; i++)
		free(auth_list[i]);
	free(auth_list);
}

static void send_local_list_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Got request for local auth list update");
	if(!cJSON_HasObjectItem(payload, "listVersion") || !cJSON_HasObjectItem(payload, "updateType")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'listVersion' and 'updateType' fields", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * list_version_json = cJSON_GetObjectItem(payload, "listVersion");
	cJSON * update_type_json = cJSON_DetachItemFromObject(payload, "updateType");

	if(!cJSON_IsString(update_type_json) || !cJSON_IsNumber(list_version_json)){

		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'listVersion' to be integer type and 'updateType' to be UpdateType", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	bool is_update_full;
	if(strcmp(update_type_json->valuestring, OCPP_UPDATE_TYPE_DIFFERENTIAL) == 0){
		is_update_full = false;
	}
	else if(strcmp(update_type_json->valuestring, OCPP_UPDATE_TYPE_FULL) == 0){
		is_update_full = true;
	}else{
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "'updateType' is not a valid UpdateType", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	struct ocpp_authorization_data ** auth_list = malloc(sizeof(struct ocpp_authorization_data *) * storage_Get_ocpp_send_local_list_max_length());

	char error_type[32];
	char error_reason[SEND_LOCAL_LIST_MAX_REASON_SIZE];
	int err = -1;

	int auth_list_length = validate_and_convert_auth_list(cJSON_GetObjectItem(payload, "localAuthorizationList"), is_update_full, auth_list, error_type, error_reason);
	if(auth_list_length == -1){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, error_type, error_reason, NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for invalid authorization data");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		free_auth_list(auth_list, auth_list_length);
		return;
	}

	if(is_update_full){
		err = fat_UpdateAuthListFull(list_version_json->valueint, auth_list, auth_list_length);

	}else{
		if(fat_ReadAuthListVersion() >= list_version_json->valueint){
			cJSON * response = ocpp_create_send_local_list_confirmation(unique_id, OCPP_UPDATE_STATUS_VERSION_MISMATCH);
			if(response == NULL){
				ESP_LOGE(TAG, "Unable to create send local list confirmation VERSION_MISMATCH");
			}else{
				send_call_reply(response);
				cJSON_Delete(response);
			}
			free_auth_list(auth_list, auth_list_length);
			return;
		}

		err = fat_UpdateAuthListDifferential(list_version_json->valueint, auth_list, auth_list_length);
	}

	cJSON * response = ocpp_create_send_local_list_confirmation(unique_id, (err == 0) ? OCPP_UPDATE_STATUS_ACCEPTED : OCPP_UPDATE_STATUS_FAILED);
	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create change configuration confirmation");
	}else{
		send_call_reply(response);
		cJSON_Delete(response);
	}
	free_auth_list(auth_list, auth_list_length);
}

static void get_local_list_version_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Got request for local auth list version");
	int version = fat_ReadAuthListVersion();

	if(version == -1){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Unable to read version from local list", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for internal error");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * response = ocpp_create_get_local_list_version_confirmation(unique_id, version);
	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create get local list confirmation");
	}else{
		send_call_reply(response);
		cJSON_Delete(response);
	}
}

const char known_vendors[0][256]; // 0 known vendors with type CiString255Type

static void data_transfer_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	if(!cJSON_HasObjectItem(payload, "vendorId")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'vendorId' field", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * vendor_id_json = cJSON_GetObjectItem(payload, "vendorId");
	if(!cJSON_IsString(vendor_id_json) || !is_ci_string_type(vendor_id_json->valuestring, 255)){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected vendorId to be CiString255Type", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	bool found_match = false;
	for(size_t i = 0; i < sizeof(known_vendors); i++){
		if(strcmp(vendor_id_json->valuestring, known_vendors[i]) == 0){
			found_match = true;
			break;
		}
	}

	if(found_match == false){
		cJSON * response = ocpp_create_data_transfer_confirmation(unique_id, OCPP_DATA_TRANSFER_STATUS_UNKNOWN_VENDOR_ID, NULL);
		if(response == NULL){
			ESP_LOGE(TAG, "Unable to respond to unknown vendor id");
		}else{
			send_call_reply(response);
			cJSON_Delete(response);
		}
		return;
	}

	// If vendor specific implementation is added, then add it here
	// Untill then, this should not happen and we return error

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Matching vendor id but no implementations found", NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create internal error response");
	}else{
		send_call_reply(ocpp_error);
		cJSON_Delete(ocpp_error);
	}
	return;
}

static void ocpp_task(){
	while(true){
		// TODO: see if there is a better way to check connectivity
		while(connectivity_GetActivateInterface() == eCONNECTION_NONE){
			ESP_LOGI(TAG, "Waiting for connection...");
			vTaskDelay(pdMS_TO_TICKS(2000));
		}
		ESP_LOGI(TAG, "Starting connection with Central System");

		int err = -1;
		unsigned int retry_attempts = 0;
		unsigned int retry_delay = 5;
		do{
			err = start_ocpp(i2cGetLoadedDeviceInfo().serialNumber,
					storage_Get_ocpp_heartbeat_interval(),
					storage_Get_ocpp_transaction_message_attempts(),
					storage_Get_ocpp_transaction_message_retry_interval());

			if(err != 0){
				if(retry_attempts < 7){
					ESP_LOGE(TAG, "Unable to open socket for ocpp, retrying in %d sec", retry_delay);
					vTaskDelay(pdMS_TO_TICKS(1000 * retry_delay));
					retry_delay *= 5;

				}else{
					ESP_LOGE(TAG, "Unable to open socket for ocpp, rebooting");
					esp_restart(); // TODO: Write reason for reboot
				}
			}
		}while(err != 0);

		set_task_to_notify(task_ocpp_handle);

		connection_status = eCS_CONNECTION_ONLINE;

		retry_attempts = 0;
		retry_delay = 5;
		do{
			err = complete_boot_notification_process(i2cGetLoadedDeviceInfo().serialNumber);
			if(err != 0){
				if(retry_attempts < 7){
					ESP_LOGE(TAG, "Unable to get accepted boot, retrying in %d sec", retry_delay);
					vTaskDelay(pdMS_TO_TICKS(1000 * retry_delay));
					retry_delay *= 5;

				}else{
					ESP_LOGE(TAG, "Unable to get accepted boot, rebooting");
					esp_restart(); // TODO: Write reason for reboot
				}
			}
		}while(err != 0);

		start_ocpp_heartbeat();

		//Indicate features that are not supported
		attach_call_cb(eOCPP_ACTION_RESERVE_NOW_ID, not_supported_cb, "Does not support reservations");
		attach_call_cb(eOCPP_ACTION_CANCEL_RESERVATION_ID, not_supported_cb, "Does not support reservations");
		attach_call_cb(eOCPP_ACTION_UNLOCK_CONNECTOR_ID, not_supported_cb, "Connector may only be disconnected from EV side");
		attach_call_cb(eOCPP_ACTION_CLEAR_CACHE_ID, not_supported_cb, "Does not support authorization cache");

		//Handle ocpp related configurations
		attach_call_cb(eOCPP_ACTION_GET_CONFIGURATION_ID, get_configuration_cb, NULL);
		attach_call_cb(eOCPP_ACTION_CHANGE_CONFIGURATION_ID, change_configuration_cb, NULL);

		//Handle features that are not bether handled by other components
		attach_call_cb(eOCPP_ACTION_RESET_ID, reset_cb, NULL);
		attach_call_cb(eOCPP_ACTION_SEND_LOCAL_LIST_ID, send_local_list_cb, NULL);
		attach_call_cb(eOCPP_ACTION_GET_LOCAL_LIST_VERSION_ID, get_local_list_version_cb, NULL);
		attach_call_cb(eOCPP_ACTION_DATA_TRANSFER_ID, data_transfer_cb, NULL);

		//Handle ClockAlignedDataInterval
		restart_clock_aligned_meter_values();

		unsigned int problem_count = 0;
		time_t last_problem_timestamp = time(NULL);
		time_t last_online_timestamp = time(NULL);
		while(true){
			uint32_t data = ulTaskNotifyTake(pdTRUE,0);

			if(data != eOCPP_WEBSOCKET_NO_EVENT){
				ESP_LOGW(TAG, "Handling websocket event");
				switch(data){
				case eOCPP_WEBSOCKET_CONNECTED:
					ESP_LOGI(TAG, "Continuing ocpp call handling");
					connection_status = eCS_CONNECTION_ONLINE;
					break;
				case eOCPP_WEBSOCKET_DISCONNECT:
					ESP_LOGW(TAG, "Websocket disconnected");

					if(connection_status == eCS_CONNECTION_ONLINE)
						last_online_timestamp = time(NULL);

					connection_status = eCS_CONNECTION_OFFLINE;
					break;
				case eOCPP_WEBSOCKET_FAILURE: // TODO: Get additional websocket errors
					ESP_LOGW(TAG, "Websocket FAILURE %d", ++problem_count);

					if(last_problem_timestamp + OCPP_PROBLEM_RESET_INTERVAL > time(NULL)){
						problem_count = 1;
					}

					last_problem_timestamp = time(NULL);
					break;
				}

				if(problem_count > OCPP_PROBLEMS_COUNT_BEFORE_RETRY)
					break;

			}

			switch(connection_status){
			case eCS_CONNECTION_ONLINE:
				if(handle_ocpp_call() != 0){
					// TODO: Improve error handling, delay was added due to send conflict with large recieve
					vTaskDelay(pdMS_TO_TICKS(1000));
				}
				break;
			case eCS_CONNECTION_OFFLINE:
				if(is_connected()){
					last_online_timestamp = time(NULL);
				        connection_status = eCS_CONNECTION_ONLINE;
				}
				else if(last_online_timestamp + OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT < time(NULL)){
					ESP_LOGE(TAG, "%d seconds since OCPP was last online, attempting reboot", OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT);
					esp_restart(); // TODO: write reason for reboot;
				}
			}
		}

		ESP_LOGE(TAG, "Exited ocpp handling, tearing down to retry");

		stop_ocpp_heartbeat();
		stop_ocpp();
	}
}

int ocpp_get_stack_watermark(){
	if(task_ocpp_handle != NULL){
		return uxTaskGetStackHighWaterMark(task_ocpp_handle);
	}else{
		return -1;
	}
}

void ocpp_init(){
	task_ocpp_handle = xTaskCreateStatic(ocpp_task, "ocpp_task", TASK_OCPP_STACK_SIZE, NULL, 2, task_ocpp_stack, &task_ocpp_buffer);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}
