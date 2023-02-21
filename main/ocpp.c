#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_crc.h"

#include "ocpp.h"
#include "connectivity.h"
#include "ppp_task.h"
#include "i2cDevices.h"
#include "storage.h"
#include "sessionHandler.h"
#include "offlineSession.h"
#include "offline_log.h"
#include "fat.h"
#include "apollo_ota.h"

#include "ocpp_listener.h"
#include "ocpp_task.h"
#include "ocpp_smart_charging.h"

#include "messages/call_messages/ocpp_call_request.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "ocpp_json/ocppj_message_structure.h"
#include "ocpp_json/ocppj_validation.h"
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
#include "types/ocpp_reason.h"
#include "types/ocpp_message_trigger.h"
#include "types/ocpp_trigger_message_status.h"
#include "types/ocpp_charge_point_error_code.h"
#include "types/ocpp_firmware_status.h"
#include "protocol_task.h"

#define TASK_OCPP_STACK_SIZE 3200
#define OCPP_PROBLEM_RESET_INTERVAL 30
#define OCPP_PROBLEMS_COUNT_BEFORE_RETRY 50
#define OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT 18000 // 5 hours
#define OCPP_EXIT_TIMEOUT 20

static const char * TAG = "OCPP";
static TaskHandle_t task_ocpp_handle = NULL;
StaticTask_t task_ocpp_buffer;
StackType_t task_ocpp_stack[TASK_OCPP_STACK_SIZE];

static bool should_run = false;
static bool should_restart = false;
static bool pending_reset = false;
static bool graceful_exit = false;
static bool connected = false;

static char * mnt_directory = "/files";
static char * firmware_update_request_path = "/files/ocpp_upd.bin";

void not_supported_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	const char * description = (const char *)cb_data;

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_NOT_SUPPORTED, description, NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create response for missing action");
		return;
	}
	send_call_reply(ocpp_error);
	return;
}

//TODO: This is run in a timer. MCU_SendCommandId blocks on semaphore with non-zero timeout, this could cause deadlock.
static void reset(){
	ESP_LOGI(TAG, "Restarting MCU");
	MessageType ret = MCU_SendCommandId(CommandReset);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU reset command OK");
	}
	else
	{
		ESP_LOGE(TAG, "MCU reset command failed");
	}
	ESP_LOGI(TAG, "Restarting esp");
	esp_restart(); 	//TODO: Write restart reason
}

void reset_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGW(TAG, "Received reset request");

	if(cJSON_HasObjectItem(payload, "type")){
		char * reset_type = cJSON_GetObjectItem(payload, "type")->valuestring;

		if(strcmp(reset_type, OCPP_RESET_TYPE_SOFT) == 0){
			cJSON * conf = ocpp_create_reset_confirmation(unique_id, OCPP_RESET_STATUS_ACCEPTED);
			if(conf == NULL){
				ESP_LOGE(TAG, "Unable to send reset confirmation");
			}
			else{
				send_call_reply(conf);
			}
			pending_reset = true;
			should_run = false;
			graceful_exit = true;
			return;
		}
		else if(strcmp(reset_type, OCPP_RESET_TYPE_HARD) == 0){
			cJSON * conf = ocpp_create_reset_confirmation(unique_id, OCPP_RESET_STATUS_ACCEPTED);
			if(conf == NULL){
				ESP_LOGE(TAG, "Unable to create reset confirmation");
			}
			else{
				send_call_reply(conf);
			}

			/*
			 * According to the specification:
			 * "The Charge Point SHALL send a StopTransaction.req for any ongoing transaction before performing
			 * the reset. If the Charge Point fails to receive a StopTransaction.conf [from] the Central System,
			 * it shall queue the StopTransaction.req."
			 *
			 * This is regardless of the type being soft or hard. But the specification also states:
			 * "At receipt of a hard reset [...] it is not required to gracefully stop ongoing transaction."
			 *
			 * It is unclear what a non gracefully stoped transaction would be if not the sending and receiving
			 * of StopTransaction request and confirmation.
			 *
			 * The approach taken here is to stop any ongoing transaction, enqueueing StopTransaction.req and
			 * waiting 2 seconds, before resetting. If the StopTransaction.req is not sendt or saved within the
			 * 2 seconds, enqueued data will be lost.
			 * According to the specification:
			 * "by sending a "hard" reset, (queued) information might get lost"
			 *
			 * The loss of information should therefore be acceptable.
			 */
			bool ongoing_transaction = false;
			for(size_t i = 1; i <= CONFIG_OCPP_NUMBER_OF_CONNECTORS; i++){
				if(sessionHandler_OcppTransactionIsActive(i)){
					sessionHandler_OcppStopTransaction(OCPP_REASON_HARD_RESET);
					ongoing_transaction = true;
				}
			}

			bool delay = false;
			if(ongoing_transaction){
				TimerHandle_t reset_timer = xTimerCreate("reset", pdMS_TO_TICKS(2000), false, NULL, reset);
				if(reset_timer != NULL){
					if(xTimerStart(reset_timer, 0) != pdPASS){
						ESP_LOGE(TAG, "Unable to start reset timer, Resetting imediatly");
						reset();
					}else{
						delay = true;
					}
				}else{
					ESP_LOGE(TAG, "Unable to create reset timer, Resetting imediatly");
				}
			}

			if(!delay){
				vTaskDelay(pdMS_TO_TICKS(400)); // Allow time for websocket to send call reply
				reset();
			}
		}
		else{
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "'type' field does not conform to 'ResetType'", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
				return;
			}
			else{
				send_call_reply(ocpp_error);
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
			return;
		}
	}
}


static int populate_sample_current_import(char * phase, enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){
	//Because the go only has 1 connector, we can get the current in the same way regardless of connector id

	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_CURRENT_IMPORT,
		.phase = eOCPP_PHASE_L1,
		.location = eOCPP_LOCATION_OUTLET,
		.unit = eOCPP_UNIT_A
	};

	size_t new_values_count = 0;

	if(phase == NULL || strcmp(phase, OCPP_PHASE_L1) == 0){
		//Phase 1
		sprintf(new_value.value, "%f", MCU_GetCurrents(0));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}


	if(phase == NULL || strcmp(phase, OCPP_PHASE_L2) == 0){
		//Phase 2
		new_value.phase = eOCPP_PHASE_L2;
		sprintf(new_value.value, "%f", MCU_GetCurrents(1));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}

	if(phase == NULL || strcmp(phase, OCPP_PHASE_L3) == 0){
		//Phase 3
		new_value.phase = eOCPP_PHASE_L3;
		sprintf(new_value.value, "%f", MCU_GetCurrents(2));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}

	return new_values_count;
}

//TODO: consider changing from using standalone current and if value should be changed as offered changes and prsence of car
static int populate_sample_current_offered(enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){

	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_CURRENT_OFFERED,
		.unit = eOCPP_UNIT_A
	};

	sprintf(new_value.value, "%f", MCU_GetChargeCurrentUserMax());
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static time_t aligned_timestamp_begin = 0;
static time_t aligned_timestamp_end = 0;
static float aligned_energy_active_import_begin = 0;
static float aligned_energy_active_import_end = 0;

static time_t sampled_timestamp_begin = 0;
static time_t sampled_timestamp_end = 0;
static float sampled_energy_active_import_begin = 0;
static float sampled_energy_active_import_end = 0;

static int populate_sample_energy_active_import_interval(enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
		.unit = eOCPP_UNIT_KWH
	};

	switch(context){
	case eOCPP_CONTEXT_SAMPLE_CLOCK:
		sprintf(new_value.value, "%f", aligned_energy_active_import_end - aligned_energy_active_import_begin);
		break;
	case eOCPP_CONTEXT_SAMPLE_PERIODIC:
	case eOCPP_CONTEXT_TRANSACTION_END:
		sprintf(new_value.value, "%f", sampled_energy_active_import_end - sampled_energy_active_import_begin);
		break;
	default:
		return 0;
	};

	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static float populate_sample_power_active_import(enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_POWER_ACTIVE_IMPORT,
		.location = eOCPP_LOCATION_OUTLET,
		.unit = eOCPP_UNIT_W
	};

	sprintf(new_value.value, "%f", MCU_GetPower());
	if(ocpp_sampled_list_add(value_list_out, new_value) == NULL)
		return 0;

	return 1;
}

static int populate_sample_temperature(char * phase, uint connector_id, enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_TEMPERATURE,
		.location = eOCPP_LOCATION_BODY,
		.unit = eOCPP_UNIT_CELSIUS
	};

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
		new_value.location = eOCPP_LOCATION_OUTLET;

		size_t new_values_count = 0;

		if(phase == NULL || strcmp(phase, OCPP_PHASE_L1) == 0){
			//phase 1
			new_value.phase = eOCPP_PHASE_L1;
			sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(0));
			if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
				new_values_count++;
		}

		if(phase == NULL || strcmp(phase, OCPP_PHASE_L2) == 0){
			//phase 2
			new_value.phase = eOCPP_PHASE_L2;
			sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(1));
			if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
				new_values_count++;
		}

		if(phase == NULL || strcmp(phase, OCPP_PHASE_L3) == 0){
			//phase 3
			new_value.phase = eOCPP_PHASE_L3;
			sprintf(new_value.value, "%f", MCU_GetEmeterTemperature(2));
			if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
				new_values_count++;
		}

		return new_values_count;
	}else{
		ESP_LOGE(TAG, "Unexpected connector id");
		return 0;
	}
}

static int populate_sample_voltage(char * phase, enum ocpp_reading_context_id context, struct ocpp_sampled_value_list * value_list_out){
	struct ocpp_sampled_value new_value = {
		.context = context,
		.format = eOCPP_FORMAT_RAW,
		.measurand = eOCPP_MEASURAND_VOLTAGE,
		.phase = eOCPP_PHASE_L1,
		.location = eOCPP_LOCATION_OUTLET,
		.unit = eOCPP_UNIT_A
	};

	size_t new_values_count = 0;
	if(phase == NULL || strcmp(phase, OCPP_PHASE_L1) == 0){
		//Phase 1
		sprintf(new_value.value, "%f", MCU_GetCurrents(0));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}

	if(phase == NULL || strcmp(phase, OCPP_PHASE_L2) == 0){
		//Phase 2
		new_value.phase = eOCPP_PHASE_L2;
		sprintf(new_value.value, "%f", MCU_GetCurrents(1));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}

	if(phase == NULL || strcmp(phase, OCPP_PHASE_L3) == 0){
		//Phase 3
		new_value.phase = eOCPP_PHASE_L3;
		sprintf(new_value.value, "%f", MCU_GetCurrents(2));
		if(ocpp_sampled_list_add(value_list_out, new_value) != NULL)
			new_values_count++;
	}

	return new_values_count;
}

void init_interval_measurands(enum ocpp_reading_context_id context){
	switch(context){
	case eOCPP_CONTEXT_SAMPLE_CLOCK:
		ESP_LOGI(TAG, "Initiating clock aligned interval measurands");

		aligned_timestamp_begin = time(NULL);
		aligned_timestamp_end = aligned_timestamp_begin;

		aligned_energy_active_import_begin = MCU_GetEnergy();
		aligned_energy_active_import_end = aligned_energy_active_import_begin;
		break;

	case eOCPP_CONTEXT_SAMPLE_PERIODIC:
	case eOCPP_CONTEXT_TRANSACTION_BEGIN:
	case eOCPP_CONTEXT_TRANSACTION_END:
		ESP_LOGI(TAG, "Initiating periodic interval measurands");

		sampled_timestamp_begin = time(NULL);
		sampled_timestamp_end = sampled_timestamp_begin;

		sampled_energy_active_import_begin = MCU_GetEnergy();
		sampled_energy_active_import_end = MCU_GetEnergy();
		break;

	default:
		ESP_LOGI(TAG, "Not updating interval measurands");
	};
}

void save_interval_measurands(enum ocpp_reading_context_id context){
	switch(context){
	case eOCPP_CONTEXT_SAMPLE_CLOCK:
		ESP_LOGI(TAG, "Updating clock aligned interval measurands");

		aligned_timestamp_begin = aligned_timestamp_end;
		aligned_timestamp_end = time(NULL);

		aligned_energy_active_import_begin = aligned_energy_active_import_end;
		aligned_energy_active_import_end = MCU_GetEnergy();
		break;

	case eOCPP_CONTEXT_SAMPLE_PERIODIC:
	case eOCPP_CONTEXT_TRANSACTION_BEGIN:
	case eOCPP_CONTEXT_TRANSACTION_END:
		ESP_LOGI(TAG, "Updating periodic interval measurands");

		sampled_timestamp_begin = sampled_timestamp_end;
		sampled_timestamp_end = time(NULL);

		sampled_energy_active_import_begin = sampled_energy_active_import_end;
		sampled_energy_active_import_end = MCU_GetEnergy();
		break;

	default:
		ESP_LOGI(TAG, "Not updating interval measurands");
	};
}

// TODO: consider adding OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER
int populate_sample(enum ocpp_measurand_id measurand, char * phase, uint connector_id, enum ocpp_reading_context_id context,
		struct ocpp_sampled_value_list * value_list_out){
	switch(measurand){
	case eOCPP_MEASURAND_CURRENT_IMPORT:
		return populate_sample_current_import(phase, context, value_list_out);
	case eOCPP_MEASURAND_CURRENT_OFFERED:
		return populate_sample_current_offered(context, value_list_out);
	case eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL:
		return populate_sample_energy_active_import_interval(context, value_list_out);
	case eOCPP_MEASURAND_POWER_ACTIVE_IMPORT:
		return populate_sample_power_active_import(context, value_list_out);
	case eOCPP_MEASURAND_TEMPERATURE:
		return populate_sample_temperature(phase, connector_id, context, value_list_out);
	case eOCPP_MEASURAND_VOLTAGE:
		return populate_sample_voltage(phase, context, value_list_out);
	default:
		ESP_LOGE(TAG, "Invalid measurand '%s'!!", ocpp_measurand_from_id(measurand));
		return 0;
	};
}

char * csl_token_get_phase_index(const char * csl_token){

	char * phase_index = rindex(csl_token, '.');
	if(phase_index == NULL){
		return NULL;

	}else if((strcasecmp(phase_index + 1, OCPP_PHASE_L1) == 0)
		|| (strcasecmp(phase_index + 1, OCPP_PHASE_L2) == 0)
		|| (strcasecmp(phase_index + 1, OCPP_PHASE_L3) == 0)){

		return phase_index;
	}else{
		return NULL;
	}
}

//TODO: "All "per-period" data [...] should be [...] transmitted [...] at the end of each interval, bearing the interval start time timestamp"
//TODO: Optionally add ISO8601 duration to meter value timestamp
int ocpp_populate_meter_values(uint connector_id, enum ocpp_reading_context_id context,
			const char * measurand_csl, struct ocpp_meter_value_list * meter_value_out){

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

	char * item = strtok(measurands, ",");

	while(item != NULL){

		ESP_LOGI(TAG, "Attempting to add %s to meter value", item);

		struct ocpp_meter_value * meter_value = malloc(sizeof(struct ocpp_meter_value));
		if(meter_value == NULL){
			ESP_LOGE(TAG, "Unable to create new meter_value");
			free(measurands);
			return -1;
		}

		meter_value->timestamp = time(NULL);
		meter_value->sampled_value = ocpp_create_sampled_list();
		if(meter_value->sampled_value == NULL){
			ESP_LOGE(TAG, "Unable to create new sample");
			free(meter_value);
			free(measurands);
			return -1;
		}
		ESP_LOGI(TAG, "Creating %s value", item);

		char * phase_index = csl_token_get_phase_index(item);
		if(phase_index != NULL){
			*phase_index = '\0'; // Terminate the item at end of measurand
			phase_index++;
		}

		int new_item_count = populate_sample(ocpp_measurand_to_id(item), phase_index, connector_id, context, meter_value->sampled_value);

		if(new_item_count < 1){
			if(new_item_count < 0)
				ESP_LOGE(TAG, "Failed to populate sample for measurand '%s'", item);

			free(meter_value->sampled_value);
			free(meter_value);
		}else{
			struct ocpp_meter_value_list * new_node = ocpp_meter_list_add_reference(meter_value_out, meter_value);
			if(new_node == NULL){
				ESP_LOGE(TAG, "Unable to add sample to meter value");
				free(meter_value->sampled_value);
				free(meter_value);
				return -1;
			}
		}

		item = strtok(NULL, ",");
	}

	free(measurands);

	return ocpp_meter_list_get_length(meter_value_out);
}

// TODO: move similarity with ocpp_populate_meter_values to new function
int ocpp_populate_meter_values_from_existing(uint connector_id, enum ocpp_reading_context_id context, const char * measurand_csl,
					struct ocpp_meter_value_list * existing_list, struct ocpp_meter_value_list * meter_value_out){

	if(measurand_csl == NULL)
		return 0;

	char * measurands = strdup(measurand_csl);
	if(measurands == NULL){
		ESP_LOGE(TAG, "Unable to duplicate measurands");
		return -1;
	}

	char * item = strtok(measurands, ",");

	while(item != NULL){
		ESP_LOGI(TAG, "Looking through existing for: '%s'", item);

		bool added = false;
		struct ocpp_meter_value_list * existing_value = existing_list;

		while(existing_value != NULL){
			// This function expects each meter value in existing list to contain only one
			// measurand, therefore we only need to check the first sample in each existing value
			if(existing_value->value == NULL
				|| existing_value->value->sampled_value == NULL
				|| existing_value->value->sampled_value->value == NULL){

				ESP_LOGE(TAG, "Expected existing list to contain sampled value");
			}
			else{
				if(ocpp_measurand_to_id(item) == existing_value->value->sampled_value->value->measurand){
					if(ocpp_meter_list_add(meter_value_out, *existing_value->value) == NULL){
						ESP_LOGE(TAG, "Unable to add exiting measurand");
					}else{
						added = true;
					}
					break;
				}
			}
			existing_value = existing_value->next;
		}

		if(added == false){
			ESP_LOGW(TAG, "Not found, creating new");
			struct ocpp_meter_value * meter_value = malloc(sizeof(struct ocpp_meter_value));
			if(meter_value == NULL){
				ESP_LOGE(TAG, "Unable to create new meter_value");
				free(measurands);
				return -1;
			}

			meter_value->timestamp = time(NULL);
			meter_value->sampled_value = ocpp_create_sampled_list();
			if(meter_value->sampled_value == NULL){
				ESP_LOGE(TAG, "Unable to create new sample");
				free(meter_value);
				free(measurands);
				return -1;
			}

			char * phase_index = csl_token_get_phase_index(item);

			if(phase_index != NULL){
				*phase_index = '\0'; // Terminate the item at end of measurand
				phase_index++;
			}

			int new_item_count = populate_sample(ocpp_measurand_to_id(item), phase_index, connector_id, context, meter_value->sampled_value);

			if(new_item_count < 1){
				if(new_item_count < 0)
					ESP_LOGE(TAG, "Failed to populate sample for measurand '%s'", item);
				free(meter_value->sampled_value);
				free(meter_value);
			}else{
				struct ocpp_meter_value_list * new_node = ocpp_meter_list_add_reference(meter_value_out, meter_value);
				if(new_node == NULL){
					ESP_LOGE(TAG, "Unable to add sample to meter value");
					free(meter_value->sampled_value);
					free(meter_value);
					return -1;
				}
			}
		}

		item = strtok(NULL, ",");
	}
	free(measurands);

	return ocpp_meter_list_get_length(meter_value_out);
}

TimerHandle_t clock_aligned_handle = NULL;

static void meter_values_response_cb(){
	ESP_LOGI(TAG, "Meter values complete");
}

static void meter_values_error_cb(){
	ESP_LOGE(TAG, "Meter values completed with errors");
}

void handle_meter_value(enum ocpp_reading_context_id context, const char * csl, const char * stoptxn_csl,
			int * transaction_id, uint * connectors, size_t connector_count){

	for(size_t i = 0; i < connector_count; i++){
		uint connector = connectors[i];
		bool transaction_related = (transaction_id != NULL && connector == 1);
		struct ocpp_meter_value_list * meter_value_list = ocpp_create_meter_list();

		if(csl != NULL && csl[0] != '\0'){
			ESP_LOGI(TAG, "Creating meter values for connector %d", connector);
			if(meter_value_list == NULL){
				ESP_LOGE(TAG, "Unable to create meter value list");
				return;
			}

			int length = ocpp_populate_meter_values(connector, context, csl, meter_value_list);

			if(length < 0){
				ESP_LOGW(TAG, "No meter values to send");

			}else{
				cJSON * request = ocpp_create_meter_values_request(connector, (transaction_related) ? transaction_id : NULL,
										meter_value_list);
				if(request == NULL){
					ESP_LOGE(TAG, "Unable to create meter value request for %s values", ocpp_reading_context_from_id(context));
					return;
				}

				ESP_LOGI(TAG, "Sending meter values");
				if(enqueue_call_immediate(request, meter_values_response_cb, meter_values_error_cb, "Meter value",
								(transaction_related) ? eOCPP_CALL_TRANSACTION_RELATED : eOCPP_CALL_GENERIC) != 0){

					ESP_LOGE(TAG, "Unable to send meter values");
					cJSON_Delete(request);

					if(transaction_related){
						ESP_LOGE(TAG, "Storing meter value on file");

						size_t meter_buffer_length;
						unsigned char * meter_buffer = ocpp_meter_list_to_contiguous_buffer(meter_value_list, false, &meter_buffer_length);
						if(meter_buffer == NULL){
							ESP_LOGE(TAG, "Could not create meter value as string");
						}else{
							if(offlineSession_SaveNewMeterValue_ocpp(*transaction_id, sessionHandler_OcppTransactionStartTime(),
													meter_buffer, meter_buffer_length)!= ESP_OK){
								ESP_LOGE(TAG, "Unable to store transaction related meter value");
							}
							free(meter_buffer);
						}

					}
				}
			}


		}

		if(stoptxn_csl != NULL && csl[0] != '\0' && transaction_id != NULL){
			ESP_LOGI(TAG, "Creating stoptxn meter values");

			struct ocpp_meter_value_list * stoptxn_meter_value_list = ocpp_create_meter_list();
			if(stoptxn_meter_value_list == NULL){
				ESP_LOGE(TAG, "Unable to create meter value list for stop transaction");
			}else{

				int length = ocpp_populate_meter_values_from_existing(connector, context, stoptxn_csl,
										meter_value_list, stoptxn_meter_value_list);
				if(length > 0){
					sessionHandler_OcppTransferMeterValues(connector, stoptxn_meter_value_list, length);
				}
			}
		}
		ocpp_meter_list_delete(meter_value_list);
	}
	save_interval_measurands(context);
}

static void clock_aligned_meter_values(){
	ESP_LOGI(TAG, "Starting clock aligned meter values");

	size_t connector_count = CONFIG_OCPP_NUMBER_OF_CONNECTORS + 1;
	uint * connectors = malloc(sizeof(uint) * connector_count);
	for(size_t i =0; i <= CONFIG_OCPP_NUMBER_OF_CONNECTORS; i++){
		connectors[i] = i;
	}

	int * transaction_id = sessionHandler_OcppGetTransactionId(1);

	handle_meter_value(eOCPP_CONTEXT_SAMPLE_CLOCK,
			storage_Get_ocpp_meter_values_aligned_data(), storage_Get_ocpp_stop_txn_aligned_data(),
			transaction_id, connectors, connector_count);

	free(connectors);
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
		xTimerStart(clock_aligned_handle, 0);
		init_interval_measurands(eOCPP_CONTEXT_SAMPLE_CLOCK);
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
		snprintf(*value_out, 4, "%u", value);
		return 0;
	}
}

static int allocate_and_write_configuration_u16(uint16_t value, char ** value_out){
	*value_out = malloc(sizeof(char) * 6);
	if(*value_out == NULL){
		return -1;
	}else{
		snprintf(*value_out, 8, "%u", value);
		return 0;
	}
}

static int allocate_and_write_configuration_u32(uint32_t value, char ** value_out){
	*value_out = malloc(sizeof(char) * 11);
	if(*value_out == NULL){
		return -1;
	}else{
		snprintf(*value_out, 16, "%u", value);
		return 0;
	}
}

int allocate_and_write_configuration_bool(bool value, char ** value_out){
	*value_out = malloc(sizeof(char) * 6);
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
static uint8_t convert_from_ocpp_phase(char L1, char L2, char L3, bool is_it){
	char phase_rotation[4];
	phase_rotation[0] = L1;
	phase_rotation[1] = L2;
	phase_rotation[2] = L3;
	phase_rotation[3] = '\0';

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

cJSON * create_key_value(const char * key, bool read_only, const char * value){
	cJSON * key_value_json = cJSON_CreateObject();
	cJSON_AddStringToObject(key_value_json, "key", key);
	cJSON_AddBoolToObject(key_value_json, "readonly", read_only);
	cJSON_AddStringToObject(key_value_json, "value", value);

	return key_value_json;
}


esp_err_t add_configuration_ocpp_allow_offline_tx_for_unknown_id(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_authorization_cache_enabled(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_authorize_remote_tx_requests(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_authorize_remote_tx_requests(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_blink_repeat(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_clock_aligned_data_interval(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u32(storage_Get_ocpp_clock_aligned_data_interval(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_connection_timeout(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u32(storage_Get_ocpp_connection_timeout(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CONNECTION_TIMEOUT, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_connector_phase_rotation(cJSON * key_list){
	char * value;
	char phase_rotation_str[CONFIG_OCPP_CONNECTOR_PHASE_ROTATION_MAX_LENGTH * 13 +1];

	size_t offset = 0;

	for(size_t i = 0; i < CONFIG_OCPP_NUMBER_OF_CONNECTORS; i++){
		if(i > 0)
			phase_rotation_str[offset++] = ',';

		sprintf(phase_rotation_str + offset, "%u.%s", i, convert_to_ocpp_phase(storage_Get_PhaseRotation()));
	}

	if(allocate_and_write_configuration_str(phase_rotation_str, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_connector_phase_rotation_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_CONNECTOR_PHASE_ROTATION_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_get_configuration_max_keys(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_GET_CONFIGURATION_MAX_KEYS, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_heartbeat_interval(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u32(storage_Get_ocpp_heartbeat_interval(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_light_intensity(cJSON * key_list){
	char * value;
	uint8_t intensity_percentage = (uint8_t)floor(storage_Get_HmiBrightness() * 100);

	if(allocate_and_write_configuration_u8(intensity_percentage, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_LIGHT_INTENSITY, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_local_authorize_offline(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_local_authorize_offline(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_local_pre_authorize(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_local_pre_authorize(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_max_energy_on_invalid_id(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_message_timeout(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u16(storage_Get_ocpp_message_timeout(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_MESSAGE_TIMEOUT, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_meter_values_aligned_data(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(storage_Get_ocpp_meter_values_aligned_data(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_meter_values_aligned_data_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_METER_VALUES_ALIGNED_DATA_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_meter_values_sampled_data(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(storage_Get_ocpp_meter_values_sampled_data(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_meter_values_sampled_data_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_METER_VALUES_SAMPLED_DATA_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_meter_value_sample_interval(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u32(storage_Get_ocpp_meter_value_sample_interval(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_minimum_status_duration(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_number_of_connectors(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_NUMBER_OF_CONNECTORS, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_reset_retries(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_reset_retries(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_RESET_RETRIES, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_transaction_max_meter_values(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_stop_transaction_on_ev_side_disconnect(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_stop_transaction_on_ev_side_disconnect(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_transaction_on_invalid_id(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_stop_transaction_on_invalid_id(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_txn_aligned_data(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(storage_Get_ocpp_stop_txn_aligned_data(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_txn_aligned_data_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(CONFIG_OCPP_STOP_TXN_ALIGNED_DATA_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_txn_sampled_data(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_stop_txn_sampled_data(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_stop_txn_sampled_data_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(CONFIG_OCPP_STOP_TXN_SAMPLED_DATA_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_supported_feature_profiles(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(CONFIG_OCPP_SUPPORTED_FEATURE_PROFILES, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_supported_feature_profiles_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_SUPPORTED_FEATURE_PROFILES_MAXL_ENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_transaction_message_attempts(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(storage_Get_ocpp_transaction_message_attempts(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_transaction_message_retry_interval(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u16(storage_Get_ocpp_transaction_message_retry_interval(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_unlock_connector_on_ev_side_disconnect(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_unlock_connector_on_ev_side_disconnect(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_websocket_ping_interval(cJSON * key_list){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t add_configuration_ocpp_supported_file_transfer_protocols(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(CONFIG_OCPP_SUPPORTED_FILE_TRANSFER_PROTOCOLS, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_SUPPORTED_FILE_TRANSFER_PROTOCOLS, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_local_auth_list_enabled(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_bool(storage_Get_ocpp_local_auth_list_enabled(), &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_local_auth_list_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u16(CONFIG_OCPP_LOCAL_AUTH_LIST_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_send_local_list_max_length(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_SEND_LOCAL_LIST_MAX_LENGTH, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_reserve_connector_zero_supported(cJSON * key_list){
#ifdef CONFIG_OCPP_RESERVE_CONNECTOR_ZERO_SUPPORTED
	char * value;
	if(allocate_and_write_configuration_bool(CONFIG_OCPP_RESERVE_CONNECTOR_ZERO_SUPPORTED, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_RESERVE_CONNECTOR_ZERO_SUPPORTED, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
#else
	return ESP_FAIL_UNKONOWN;
#endif
}

esp_err_t add_configuration_ocpp_charge_profile_max_stack_level(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CHARGE_PROFILE_MAX_STACK_LEVEL, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_charging_schedule_allowed_charging_rate_unit(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_str(CONFIG_OCPP_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_charging_schedule_max_periods(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CHARGING_SCHEDULE_MAX_PERIODS, true, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

esp_err_t add_configuration_ocpp_connector_switch_3_to_1_phase_supported(cJSON * key_list){
#ifdef CONFIG_OCPP_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED
	char * value;
	if(allocate_and_write_configuration_bool(CONFIG_OCPP_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
#else
	return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t add_configuration_ocpp_max_charging_profiles_installed(cJSON * key_list){
	char * value;
	if(allocate_and_write_configuration_u8(CONFIG_OCPP_MAX_CHARGING_PROFILES_INSTALLED, &value) != 0)
		return ESP_FAIL;

	cJSON * key_value_json = create_key_value(OCPP_CONFIG_KEY_MAX_CHARGING_PROFILES_INSTALLED, false, value);
	if(cJSON_AddItemToArray(key_list, key_value_json) != true){
		return ESP_FAIL;
	}else{
		return ESP_OK;
	}
}

static esp_err_t get_ocpp_configuration(const char * key, cJSON * configuration_out){

	if(strcasecmp(key, OCPP_CONFIG_KEY_ALLOW_OFFLINE_TX_FOR_UNKNOWN_ID) == 0){
		return add_configuration_ocpp_allow_offline_tx_for_unknown_id(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_AUTHORIZATION_CACHE_ENABLED) == 0){
		return add_configuration_ocpp_authorization_cache_enabled(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS) == 0){
		return add_configuration_ocpp_authorize_remote_tx_requests(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_BLINK_REPEAT) == 0){
		return  add_configuration_ocpp_blink_repeat(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL) == 0){
		return add_configuration_ocpp_clock_aligned_data_interval(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT) == 0){
		return add_configuration_ocpp_connection_timeout(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION) == 0){
		return add_configuration_ocpp_connector_phase_rotation(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH) == 0){
		return add_configuration_ocpp_connector_phase_rotation_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS) == 0){
		return add_configuration_ocpp_get_configuration_max_keys(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL) == 0){
		return add_configuration_ocpp_heartbeat_interval(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LIGHT_INTENSITY) == 0){
		return add_configuration_ocpp_light_intensity(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE) == 0){
		return add_configuration_ocpp_local_authorize_offline(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE) == 0){
		return add_configuration_ocpp_local_pre_authorize(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_MAX_ENERGY_ON_INVALID_ID) == 0){
		return add_configuration_ocpp_max_energy_on_invalid_id(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_MESSAGE_TIMEOUT) == 0){
		return add_configuration_ocpp_message_timeout(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA) == 0){
		return add_configuration_ocpp_meter_values_aligned_data(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH) == 0){
		return add_configuration_ocpp_meter_values_aligned_data_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA) == 0){
		return add_configuration_ocpp_meter_values_sampled_data(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH) == 0){
		return add_configuration_ocpp_meter_values_sampled_data(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL) == 0){
		return add_configuration_ocpp_meter_value_sample_interval(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_MINIMUM_STATUS_DURATION) == 0){
		return add_configuration_ocpp_minimum_status_duration(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS) == 0){
		return add_configuration_ocpp_number_of_connectors(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_RESET_RETRIES) == 0){
		return add_configuration_ocpp_reset_retries(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_MAX_METER_VALUES) == 0){
		return add_configuration_ocpp_stop_transaction_max_meter_values(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT) == 0){
		return add_configuration_ocpp_stop_transaction_on_ev_side_disconnect(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID) == 0){
		return add_configuration_ocpp_stop_transaction_on_invalid_id(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA) == 0){
		return add_configuration_ocpp_stop_txn_aligned_data(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH) == 0){
		return add_configuration_ocpp_stop_txn_aligned_data_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA) == 0){
		return add_configuration_ocpp_stop_txn_sampled_data(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA_MAX_LENGTH) == 0){
		return add_configuration_ocpp_stop_txn_sampled_data_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES) == 0){
		return add_configuration_ocpp_supported_feature_profiles(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH) == 0){
		return add_configuration_ocpp_supported_feature_profiles_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS) == 0){
		return add_configuration_ocpp_transaction_message_attempts(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL) == 0){
		return add_configuration_ocpp_transaction_message_retry_interval(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT) == 0){
		return add_configuration_ocpp_unlock_connector_on_ev_side_disconnect(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_WEBSOCKET_PING_INTERVAL) == 0){
		return add_configuration_ocpp_websocket_ping_interval(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_SUPPORTED_FILE_TRANSFER_PROTOCOLS) == 0){
		return add_configuration_ocpp_supported_file_transfer_protocols(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED) == 0){
		return add_configuration_ocpp_local_auth_list_enabled(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH) == 0){
		return add_configuration_ocpp_local_auth_list_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH) == 0){
		return add_configuration_ocpp_send_local_list_max_length(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_RESERVE_CONNECTOR_ZERO_SUPPORTED) == 0){
		return add_configuration_ocpp_reserve_connector_zero_supported(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CHARGE_PROFILE_MAX_STACK_LEVEL) == 0){
		return add_configuration_ocpp_charge_profile_max_stack_level(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT) == 0){
		return add_configuration_ocpp_charging_schedule_allowed_charging_rate_unit(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CHARGING_SCHEDULE_MAX_PERIODS) == 0){
		return add_configuration_ocpp_charging_schedule_max_periods(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED) == 0){
		return add_configuration_ocpp_connector_switch_3_to_1_phase_supported(configuration_out);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_MAX_CHARGING_PROFILES_INSTALLED) == 0){
		return add_configuration_ocpp_max_charging_profiles_installed(configuration_out);
	}else{
		return ESP_ERR_NOT_SUPPORTED;
	}
}

void get_all_ocpp_configurations(cJSON * configuration_out){
	add_configuration_ocpp_allow_offline_tx_for_unknown_id(configuration_out);
	add_configuration_ocpp_authorization_cache_enabled(configuration_out);
	add_configuration_ocpp_authorize_remote_tx_requests(configuration_out);
	add_configuration_ocpp_blink_repeat(configuration_out);
	add_configuration_ocpp_clock_aligned_data_interval(configuration_out);
	add_configuration_ocpp_connection_timeout(configuration_out);
	add_configuration_ocpp_connector_phase_rotation(configuration_out);
	add_configuration_ocpp_connector_phase_rotation_max_length(configuration_out);
	add_configuration_ocpp_get_configuration_max_keys(configuration_out);
	add_configuration_ocpp_heartbeat_interval(configuration_out);
	add_configuration_ocpp_light_intensity(configuration_out);
	add_configuration_ocpp_local_authorize_offline(configuration_out);
	add_configuration_ocpp_local_pre_authorize(configuration_out);
	add_configuration_ocpp_max_energy_on_invalid_id(configuration_out);
	add_configuration_ocpp_message_timeout(configuration_out);
	add_configuration_ocpp_meter_values_aligned_data(configuration_out);
	add_configuration_ocpp_meter_values_aligned_data_max_length(configuration_out);
	add_configuration_ocpp_meter_values_sampled_data(configuration_out);
	add_configuration_ocpp_meter_values_sampled_data_max_length(configuration_out);
	add_configuration_ocpp_meter_value_sample_interval(configuration_out);
	add_configuration_ocpp_minimum_status_duration(configuration_out);
	add_configuration_ocpp_number_of_connectors(configuration_out);
	add_configuration_ocpp_reset_retries(configuration_out);
	add_configuration_ocpp_stop_transaction_max_meter_values(configuration_out);
	add_configuration_ocpp_stop_transaction_on_ev_side_disconnect(configuration_out);
	add_configuration_ocpp_stop_transaction_on_invalid_id(configuration_out);
	add_configuration_ocpp_stop_txn_aligned_data(configuration_out);
	add_configuration_ocpp_stop_txn_aligned_data_max_length(configuration_out);
	add_configuration_ocpp_stop_txn_sampled_data(configuration_out);
	add_configuration_ocpp_stop_txn_sampled_data_max_length(configuration_out);
	add_configuration_ocpp_supported_feature_profiles(configuration_out);
	add_configuration_ocpp_supported_feature_profiles_max_length(configuration_out);
	add_configuration_ocpp_transaction_message_attempts(configuration_out);
	add_configuration_ocpp_transaction_message_retry_interval(configuration_out);
	add_configuration_ocpp_unlock_connector_on_ev_side_disconnect(configuration_out);
	add_configuration_ocpp_websocket_ping_interval(configuration_out);
	add_configuration_ocpp_supported_file_transfer_protocols(configuration_out);
	add_configuration_ocpp_local_auth_list_enabled(configuration_out);
	add_configuration_ocpp_local_auth_list_max_length(configuration_out);
	add_configuration_ocpp_send_local_list_max_length(configuration_out);
	add_configuration_ocpp_reserve_connector_zero_supported(configuration_out);
	add_configuration_ocpp_charge_profile_max_stack_level(configuration_out);
	add_configuration_ocpp_charging_schedule_allowed_charging_rate_unit(configuration_out);
	add_configuration_ocpp_charging_schedule_max_periods(configuration_out);
	add_configuration_ocpp_connector_switch_3_to_1_phase_supported(configuration_out);
	add_configuration_ocpp_max_charging_profiles_installed(configuration_out);
}

static void get_configuration_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Got request for get configuration");

	char err_str[128];
	enum ocppj_err_t err = eOCPPJ_NO_ERROR;

	int key_length = 0;

	if(cJSON_HasObjectItem(payload, "key")){
		cJSON * key = cJSON_GetObjectItem(payload, "key");

		if(!cJSON_IsArray(key)){
			err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
			sprintf(err_str, "Expected 'key' to be a valid array");

			goto error;
		}

		key_length = cJSON_GetArraySize(key);

		if(key_length > CONFIG_OCPP_GET_CONFIGURATION_MAX_KEYS){
			err = eOCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION;
			snprintf(err_str, sizeof(err_str), "Length of 'key' (%d) exceed maximum lenght (%d)", key_length, CONFIG_OCPP_GET_CONFIGURATION_MAX_KEYS);

			goto error;
		}

		for(size_t i = 0; i < key_length; i++){
			cJSON * key_name = cJSON_GetArrayItem(key, i);
			if(!cJSON_IsString(key_name) || !is_ci_string_type(key_name->valuestring, 50)){
				err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
				sprintf(err_str, "key %d is not a valid CiString50Type", i);

				goto error;
			}
		}
	}

	cJSON * configuration_key = cJSON_CreateArray();
	if(configuration_key == NULL){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "Unable to allocate memory for configurationKey");
	}

	size_t unknown_key_index = 0;
	char * unknown_key[CONFIG_OCPP_GET_CONFIGURATION_MAX_KEYS] = {0};

	if(key_length == 0){
		get_all_ocpp_configurations(configuration_key);

	}else if(key_length > 0){
		cJSON * key = cJSON_GetObjectItem(payload, "key");

		for(size_t i = 0; i < key_length; i++){
			char * key_str = cJSON_GetArrayItem(key, i)->valuestring;
			esp_err_t config_err = get_ocpp_configuration(key_str, configuration_key);

			if(config_err != ESP_OK){
				if(config_err != ESP_ERR_NOT_SUPPORTED){
					ESP_LOGE(TAG, "Unexpected result when getting configuration: %s", esp_err_to_name(config_err));
				}

				unknown_key[unknown_key_index] = malloc(sizeof(char) * strlen(key_str) + 1);
				if(unknown_key[unknown_key_index] == NULL){
					ESP_LOGE(TAG, "Unable to allocate buffer for unknown key");
				}else{
					strcpy(unknown_key[unknown_key_index], key_str);
					unknown_key_index++;
				}
			}
		}
	}

	cJSON * response = ocpp_create_get_configuration_confirmation(unique_id, configuration_key, unknown_key_index, unknown_key);

	for(size_t i = 0; i < unknown_key_index; i++){
		free(unknown_key[i]);
	}

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create configuration response");
		goto error;
	}else{
		send_call_reply(response);
	}

	return;

error:

	if(err == ESP_OK){
		ESP_LOGE(TAG, "get_configuration_cb reached error exit, but no error set");
		err = eOCPPJ_ERROR_INTERNAL;
		err_str[0] = '\0';
	}else{
		ESP_LOGE(TAG, "get_configuration_cb reached error exit: [%s]: '%s'", ocppj_error_code_from_id(err), err_str);
	}

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create call error for internal error");
	}else{
		send_call_reply(ocpp_error);
	}
}

static void change_config_confirm(const char * unique_id, const char * configuration_status){
	cJSON * response = ocpp_create_change_configuration_confirmation(unique_id, configuration_status);
	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create change configuration confirmation");
		return;
	}else{
		send_call_reply(response);
	}
}

static bool is_valid_alignment_interval(uint32_t sec){
	if(sec == 0)
		return true;

	return (86400 % sec) == 0 ? true : false;
}

static bool is_true(bool value){
	return value;
}

static long validate_u(const char * value, uint32_t upper_bounds){
	char * endptr;

	errno = 0;
	long value_long = strtol(value, &endptr, 0);
	if(errno != 0 || endptr[0] != '\0'){
		ESP_LOGE(TAG, "Not a valid unsigned integer: %s", strerror(errno));
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

static int set_config_bool(void (*config_function)(bool), const char * value, bool (*additional_validation)(bool)){
	bool boolean_value;

	if(strcasecmp(value, "true") == 0){
		boolean_value = true;
	}else if(strcasecmp(value, "false") == 0){
		boolean_value = false;
	}else{
		return -1;
	}

	if(additional_validation != NULL && additional_validation(boolean_value) == false)
		return -1;

	config_function(boolean_value);
	return 0;
}

bool csl_expect_phase(const char * option){
	if(strcmp(option, OCPP_MEASURAND_CURRENT_IMPORT) == 0
		|| strcmp(option, OCPP_MEASURAND_TEMPERATURE) == 0
		|| strcmp(option, OCPP_MEASURAND_VOLTAGE) == 0
		){

		return true;
	}else{
		return false;
	}
}

static int set_config_csl(void (*config_function)(const char *), const char * value, uint8_t max_items, size_t option_count, ...){
	//Check if given any configurations
	size_t len = strlen(value);

	if(len == 0){
		ESP_LOGW(TAG, "Clearing configuration");
		config_function("");
		return 0;
	}

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

	char * config_position = config_str; // used to change case insensensitive input to case sensitive.

	// Check if each item is among options
	char * token = strtok(value_prepared, ",");
	while(token != NULL){
		va_list argument_ptr;
		bool is_valid = false;
		const char * enum_value;

		va_start(argument_ptr, option_count);
		for(int i = 0; i < option_count; i++){
			 enum_value = va_arg(argument_ptr, const char *);
			if(strncasecmp(token, enum_value, strlen(enum_value)) == 0){
				is_valid = true;

				strncpy(config_position, enum_value, strlen(enum_value)); // Use case sensitive version
				config_position += strlen(enum_value);

				break;
			}
		}

		va_end(argument_ptr);

		if(!is_valid){
			ESP_LOGW(TAG, "CSL contained invalid item: '%s'", token);
			goto error;
		}

		char phase_buffer[6] = {0};

		if(csl_expect_phase(enum_value)){
			const char * phase_index = csl_token_get_phase_index(token);
			if((phase_index != NULL && phase_index[3] != '\0')
				|| (phase_index == NULL && token[strlen(enum_value)] != '\0')){

				ESP_LOGW(TAG, "CSL item did not end after %s", (phase_buffer[0] == 0) ? "token" : "phase");
				goto error;
			}else if(phase_index != NULL){
				config_position[1] = 'L'; // Make sure the 'L' in .L1, .L2 or .L3 is uppercase
			}
		}else{
			if(token[strlen(enum_value)] != '\0'){
				ESP_LOGW(TAG, "Initial part of CSL contained valid item, but contains unsupported continuation");
				goto error;
			}
		}

		token = strtok(NULL, ",");
		config_position = index(config_position, ',');
		if(config_position != NULL)
			config_position++;
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
			return;
		}

	}

	const char * key = key_json->valuestring;
	const char * value = value_json->valuestring;

	ESP_LOGI(TAG, "Given configuration: \n\tkey: '%s'\n\tvalue: '%s'", key, value);
	int err = -1;
	if(strcasecmp(key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS) == 0){
		err = set_config_bool(storage_Set_ocpp_authorize_remote_tx_requests, value, NULL);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_clock_aligned_data_interval, value, is_valid_alignment_interval);
		if(err == 0)
			restart_clock_aligned_meter_values();

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT) == 0){
		err = set_config_u32(storage_Set_ocpp_connection_timeout, value, NULL);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION) == 0){
		/*
		 * The go represent connector phase rotation via wire index.
		 * OCPP uses three letters representing L1, L2, L3 optionally
		 * prefixed by the connector. As the Go only has one connecrot,
		 * it has been decided that it will reject a request to set
		 * connector 0 to a different value than connector 1, and
		 * setting either will be reported as both values updated.
		 */

		bool is_valid = true;
		size_t value_count = 1;
		uint current_connector_id = 0;

		char L1 = '\0';
		char L2 = '\0';
		char L3 = '\0';

		size_t current_item_phase_count = 0;

		size_t data_length = strlen(value);
		for(size_t i = 0; i < data_length; i++){
			if(isspace(value[i])){
				// skip space;
			}else if(isdigit(value[i])){ // connector id
				if(current_item_phase_count == 0){ // if not expecting phase value
					current_connector_id = value[i] - '0';
					if(current_connector_id > CONFIG_OCPP_NUMBER_OF_CONNECTORS){
						is_valid = false;
						break;
					}
				}else{
					is_valid = false;
					break;
				}
			}else if(isupper(value[i])){ // phase value
				if(value[i] == 'R' || value[i] == 'S' || value[i] == 'T'){
					switch(++current_item_phase_count){
					case 1:
						if(L1 == '\0'){
							L1 = value[i];
						}else{
							is_valid = (L1 == value[i]);
						}
						break;
					case 2:
						if(L2 == '\0'){
							L2 = value[i];
						}else{
							is_valid = (L2 == value[i]);
						}
						break;
					case 3:
						if(L3 == '\0'){
							L3 = value[i];
						}else{
							is_valid = (L3 == value[i]);
						}
						break;
					default: // not expecting more phase values
						is_valid = false;
					}

					if(is_valid == false)
						break;
				}else{ // invalid phase value
					is_valid = false;
					break;
				}
			}else if(ispunct(value[i])){
				switch(value[i]){
				case '.':
					if(current_item_phase_count != 0)
						is_valid = false;
					break;
				case ',':
					if(current_item_phase_count == 3){ // Phase rotation item complete
						current_item_phase_count = 0;
						if(++value_count > CONFIG_OCPP_CONNECTOR_PHASE_ROTATION_MAX_LENGTH){
							is_valid = false;
						}
					}else{
						is_valid = false;
					}
					break;
				default:
					is_valid = false;
				}

				if(is_valid == false){
					break;
				}
			}else if(value[i] == '\0'){
				break;
			}else{
				is_valid = false;
				break;
			}
		}

		//TODO: find better way to check if it should be TN or IT connector wiring
		uint8_t wire_index = convert_from_ocpp_phase(L1, L2, L3, storage_Get_PhaseRotation() > 9);

		if(!is_valid || current_item_phase_count != 3 || wire_index == 0){
			err = -1;
		}else{

			storage_Set_PhaseRotation(wire_index);
			err = 0;
		}
	}else if(strcasecmp(key, OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_heartbeat_interval, value, NULL);
		if(err == 0)
			update_heartbeat_timer(storage_Get_ocpp_heartbeat_interval());

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LIGHT_INTENSITY) == 0){
		char * endptr;
		long value_long = strtol(value, &endptr, 0);

		if(endptr[0] == '0' || value_long < 0 || value_long > 100){
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
			return;
		}

		float intensity = value_long / 100.0f;

		MessageType ret = MCU_SendFloatParameter(HmiBrightness, intensity);
		if(ret == MsgWriteAck)
		{
			ESP_LOGI(TAG, "Set hmiBrightness: %f", intensity);

			storage_Set_HmiBrightness(value_long / 100.0f);
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);
			return;
		}else{
			ESP_LOGE(TAG, "Unable to change hmi brightness");
			change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
			return;
		}


	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE) == 0){
		err = set_config_bool(storage_Set_ocpp_local_authorize_offline, value, NULL);

		if(err == 0){
			ocpp_change_message_timeout(storage_Get_ocpp_message_timeout());
		}

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE) == 0){
		err = set_config_bool(storage_Set_ocpp_local_pre_authorize, value, NULL);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_MESSAGE_TIMEOUT) == 0){
		err = set_config_u16(storage_Set_ocpp_message_timeout, value);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_meter_values_aligned_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

		// TODO: "where applicable, the Measurand is combined with the optional phase; for instance: Voltage.L1"
	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_meter_values_sampled_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_meter_value_sample_interval, value, NULL);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_RESET_RETRIES) == 0){
		err = set_config_u8(storage_Set_ocpp_reset_retries, value);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT) == 0){
		/*
		 * NOTE: Current behaviour of mcu in regards to querying chargesession value makes it so that
		 * StopTransactionOnEvSideDisconnect 'false' becomes complicated to implement. For now true is required.
		 * It is still read/write as required by ocpp 1.6.
		 */
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_ev_side_disconnect, value, is_true);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID) == 0){
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_invalid_id, value, NULL);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_aligned_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_sampled_data, value, DEFAULT_CSL_LENGTH, 6,
				OCPP_MEASURAND_CURRENT_IMPORT,
				OCPP_MEASURAND_CURRENT_OFFERED,
				OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
				OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
				OCPP_MEASURAND_TEMPERATURE,
				OCPP_MEASURAND_VOLTAGE
				);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS) == 0){
		err = set_config_u8(storage_Set_ocpp_transaction_message_attempts, value);
		if(err == 0)
			update_transaction_message_related_config(
				storage_Get_ocpp_transaction_message_attempts(),
				storage_Get_ocpp_transaction_message_retry_interval());

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL) == 0){
		err = set_config_u16(storage_Set_ocpp_transaction_message_retry_interval, value);
		if(err == 0)
			update_transaction_message_related_config(
				storage_Get_ocpp_transaction_message_attempts(),
				storage_Get_ocpp_transaction_message_retry_interval());

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT) == 0){
		/*
		 * NOTE: Current behaviour of mcu in regards to connector makes it impossible to connect/disconnect
		 * connector from esp. 'true' is therefore the only value we accept. An alternative would be to
		 * permanently lock the connector.
		 * It is still read/write as required by ocpp 1.6.
		 */
		err = set_config_bool(storage_Set_ocpp_unlock_connector_on_ev_side_disconnect, value, is_true);

	}else if(strcasecmp(key, OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED) == 0){
		err = set_config_bool(storage_Set_ocpp_local_auth_list_enabled, value, NULL);

	}else if(is_configuration_key(key)){
		ESP_LOGW(TAG, "Change configuration request rejected due to rejected key: '%s'", key);
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
		return;
	}else{
		ESP_LOGW(TAG, "Change configuration for key: '%s' is not supported", key);
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_NOT_SUPPORTED);
		return;
	}

	if(err == 0){
		ESP_LOGI(TAG, "Successfully configured %s", key);
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);
		storage_SaveConfiguration();
	}else{
		ESP_LOGW(TAG, "Unsuccessfull in configuring %s", key);
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
		if(!cJSON_IsString(status_json) || ocpp_validate_enum(status_json->valuestring, true, 4,
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
	if(tag_count > CONFIG_OCPP_SEND_LOCAL_LIST_MAX_LENGTH){
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
			if(strcasecmp(item_out->id_tag, data_out[j]->id_tag) == 0){
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
		}
		return;
	}

	struct ocpp_authorization_data ** auth_list = malloc(sizeof(struct ocpp_authorization_data *) * CONFIG_OCPP_SEND_LOCAL_LIST_MAX_LENGTH);

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
		}
		return;
	}

	cJSON * response = ocpp_create_get_local_list_version_confirmation(unique_id, version);
	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create get local list confirmation");
	}else{
		send_call_reply(response);
	}
}

#define KNOWN_VENDOR_COUNT 0
const char known_vendors[KNOWN_VENDOR_COUNT][256]; // 0 known vendors with type CiString255Type

static void data_transfer_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	if(!cJSON_HasObjectItem(payload, "vendorId")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'vendorId' field", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
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
		}
		return;
	}

	bool found_match = false;
#if KNOWN_VENDOR_COUNT > 0
	for(size_t i = 0; i < KNOWN_VENDOR_COUNT; i++){
		if(strcmp(vendor_id_json->valuestring, known_vendors[i]) == 0){
			found_match = true;
			break;
		}
	}
#endif /* KNOWN_VENDOR_COUNT > 0 */
	if(found_match == false){
		cJSON * response = ocpp_create_data_transfer_confirmation(unique_id, OCPP_DATA_TRANSFER_STATUS_UNKNOWN_VENDOR_ID, NULL);
		if(response == NULL){
			ESP_LOGE(TAG, "Unable to respond to unknown vendor id");
		}else{
			send_call_reply(response);
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
	}
	return;
}

void ocpp_send_connector_zero_status(const char * error_code, char * info){

	const char * state = (storage_Get_IsEnabled() && get_registration_status() == eOCPP_REGISTRATION_ACCEPTED) ? OCPP_CP_STATUS_AVAILABLE : OCPP_CP_STATUS_UNAVAILABLE;

	cJSON * status_notification  = ocpp_create_status_notification_request(0, error_code, info, state, time(NULL), NULL, NULL);
	if(status_notification == NULL){
		ESP_LOGE(TAG, "Unable to create status notification request");
	}else{
		int err = enqueue_call(status_notification, NULL, NULL, "status notification", eOCPP_CALL_GENERIC);
		if(err != 0){
			ESP_LOGE(TAG, "Unable to enqueue status notification");
			cJSON_Delete(status_notification);
		}
	}

}

static void trigger_message_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Recieved trigger message request");

	if(!cJSON_HasObjectItem(payload, "requestedMessage")){
		ESP_LOGW(TAG, "Trigger message lacks 'requestedMessage'");
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'requestedMessage' field", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
		}
		return;
	}

	cJSON * trigger_message_json = cJSON_GetObjectItem(payload, "requestedMessage");
	if(!cJSON_IsString(trigger_message_json) || ocpp_validate_enum(trigger_message_json->valuestring, true, 6,
									OCPP_MESSAGE_TRIGGER_BOOT_NOTIFICATION,
									OCPP_MESSAGE_TRIGGER_DIAGNOSTICS_STATUS_NOTIFICATION,
									OCPP_MESSAGE_TRIGGER_FIRMWARE_STATUS_NOTIFICATION,
									OCPP_MESSAGE_TRIGGER_HEARTBEAT,
									OCPP_MESSAGE_TRIGGER_METER_VALUES,
									OCPP_MESSAGE_TRIGGER_STATUS_NOTIFICATION) != 0){
		ESP_LOGW(TAG, "Requested message is a MessageTrigger type");
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION,
							"Expected 'requestedMessage' to be 'MessageTrigger' type", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
		}
		return;
	}

	uint * connector_id = NULL;
	if(cJSON_HasObjectItem(payload, "connectorId")){
		cJSON * connector_id_json = cJSON_GetObjectItem(payload, "connectorId");
		if(cJSON_IsNumber(connector_id_json) && connector_id_json->valueint > 0 && connector_id_json->valueint <= CONFIG_OCPP_NUMBER_OF_CONNECTORS){
			connector_id = (uint *)&connector_id_json->valueint;
		}else{
			ESP_LOGE(TAG, "Trigger request contains invalid connectorId. connectorId will be ignored");
		}
	}

	cJSON * conf =  ocpp_create_trigger_message_confirmation(unique_id, OCPP_TRIGGER_MESSAGE_STATUS_ACCEPTED);
	if(conf == NULL){
		ESP_LOGE(TAG, "Unable to create confirmation for trigger message");
	}else{
		send_call_reply(conf);
	}

	const char * requested_message = trigger_message_json->valuestring;

	if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_BOOT_NOTIFICATION) == 0){
		enqueue_boot_notification();

	}else if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_DIAGNOSTICS_STATUS_NOTIFICATION) == 0){
		send_diagnostics_status_notification();

	}else if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_FIRMWARE_STATUS_NOTIFICATION) == 0){
		cJSON * call = NULL;
		if(otaIsRunning()){
			call = ocpp_create_firmware_status_notification_request(OCPP_FIRMWARE_STATUS_DOWNLOADING);

		}else{
			call = ocpp_create_firmware_status_notification_request(OCPP_FIRMWARE_STATUS_IDLE);
		}

		if(call == NULL) {
			ESP_LOGE(TAG, "Unable to create firmware status notification");
		}else{
			if(enqueue_call(call, NULL, NULL, NULL, eOCPP_CALL_GENERIC) != 0){
				ESP_LOGE(TAG, "Unable to enqueue triggered firmware status notification");
				cJSON_Delete(call);
			}
		}

	}else if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_HEARTBEAT) == 0){
		ocpp_heartbeat();

	}else if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_METER_VALUES) == 0){
		bool allocated = false;
		size_t connector_count = 1;

		if(connector_id == NULL){
			connector_count = CONFIG_OCPP_NUMBER_OF_CONNECTORS + 1;
			connector_id = malloc(sizeof(uint) * connector_count);

			if(connector_id == NULL){
				ESP_LOGE(TAG, "Unable to allocate memory for all connectors triggered with meter values");
				return;
			}
			allocated = true;

			for(size_t i = 0; i <= CONFIG_OCPP_NUMBER_OF_CONNECTORS; i++){
				connector_id[i] = i;
			}
		}

		handle_meter_value(eOCPP_CONTEXT_TRIGGER, storage_Get_ocpp_meter_values_sampled_data(),
				NULL, NULL, connector_id, connector_count);

		if(allocated)
			free(connector_id);

	}else if(strcmp(requested_message, OCPP_MESSAGE_TRIGGER_STATUS_NOTIFICATION) == 0){
		if(connector_id == NULL){
			ocpp_send_connector_zero_status(OCPP_CP_ERROR_NO_ERROR, NULL);
			sessionHandler_OcppSendState();

		}else if(*connector_id == 0){
			/*
			 * The specification states: "a request for a statusNotification for connectorId 0 is
			 * a request for the status of the Charge Point." But it also states that connectorId
			 * is an integer greater than 0. This if clause should therefore never be executed and
			 * the request can never be granted.
			 */
			ocpp_send_connector_zero_status(OCPP_CP_ERROR_NO_ERROR, NULL);

		}else if(*connector_id == 1){
			sessionHandler_OcppSendState();

		}else{
			ESP_LOGE(TAG, "Unhandled connector id for status notification");
		}
	}

}

enum ocpp_main_event{
	eOCPP_FIRMWARE_UPDATE = 1<<0,
	eOCPP_QUIT = 1<<1
};

static void ocpp_prepare_firmware_update(){
	xTaskNotify(task_ocpp_handle, eOCPP_FIRMWARE_UPDATE, eSetBits);
}

TimerHandle_t firmware_update_handle = NULL;

int defer_update(time_t when){
	time_t now = time(NULL);
	if(when > now){
		int update_delay = when - now;
		firmware_update_handle = xTimerCreate("Ocpp firmware update",
						pdMS_TO_TICKS(update_delay * 1000),
						pdFALSE, NULL, ocpp_prepare_firmware_update);

		if(firmware_update_handle == NULL || xTimerStart(firmware_update_handle, pdMS_TO_TICKS(200)) != pdTRUE){
			ESP_LOGE(TAG, "Unable to activate new firmware update timer");
			return -1;
		}else{
			int days = update_delay / 24*60*60;
			update_delay %= 24*60*60;
			int hours = update_delay / 60*60;
			update_delay %= 60*60;
			int minutes = update_delay / 60;
			update_delay %= 60;
			ESP_LOGI(TAG, "Update will start in %d days %d hours %d minutes and %d seconds",  days, hours, minutes, update_delay);
		}
	}else{
		ocpp_prepare_firmware_update();
	}

	return 0;
}

struct update_request{
	char location[1024];
	int retries;
	time_t retrieve_date;
	int retry_interval;
};

static struct update_request update_info = {0};

static int save_update_request(struct update_request * request){
	int ret = 0;

	struct stat st;
	if(stat(mnt_directory, &st) != 0)
		return ENOTDIR;

	FILE* fp = fopen(firmware_update_request_path, "wb");
	if(fp == NULL)
		return errno;

	uint32_t crc_calc = esp_crc32_le(0, (uint8_t *)request, sizeof(struct update_request));

	if(fwrite(request, sizeof(struct update_request), 1, fp) != 1){
		ret = errno;
		goto error;
	}

	if(fwrite(&crc_calc, sizeof(uint32_t), 1, fp) != 1){
		ret = errno;
		goto error;
	}

	return fclose(fp);
error:
	fclose(fp);
	remove(firmware_update_request_path);

	return ret;
}

static int load_update_request(struct update_request * request){
	int ret = 0;

	struct stat st;
	if(stat(mnt_directory, &st) != 0)
		return ENOTDIR;

	FILE* fp = fopen(firmware_update_request_path, "rb");
	if(fp == NULL)
		return errno;

	if(fread(request, sizeof(struct update_request), 1, fp) != 1){
		ret = errno;
		goto error;
	}

	uint32_t crc_read;
	if(fread(&crc_read, sizeof(uint32_t), 1, fp) != 1){
		ret = errno;
		goto error;
	}

	uint32_t crc_calc = esp_crc32_le(0, (uint8_t *)request, sizeof(struct update_request));

	if(crc_calc != crc_read){
		ret = EINVAL;
		goto error;
	}

	return fclose(fp);
error:
	fclose(fp);
	remove(firmware_update_request_path);

	return ret;
}

static void update_firmware_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request to update firmware");
	char err_str[128];

	char * location = NULL;
	enum ocppj_err_t err = ocppj_get_string_field(payload, "location", true, &location, err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_ERROR){
		ESP_LOGW(TAG, "Unable to get 'location' from payload: %s", err_str);
		goto error;
	}

	if(strlen(location)+1 > sizeof(update_info.location)){
		snprintf(err_str, sizeof(err_str), "'location' too long. Firmware only supports %d", sizeof(update_info.location)-1);
		err = eOCPPJ_ERROR_NOT_SUPPORTED;
		ESP_LOGW(TAG, "Invalid location: %s", err_str);

		goto error;
	}

	//TODO: add aditional verification of location
	strcpy(update_info.location, location);

	err = ocppj_get_int_field(payload, "retries", false, &update_info.retries, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_VALUE){
		if(err != eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Unable to get 'retries' from payload: %s", err_str);
			goto error;
		}
	}else {
		update_info.retries = 0;
	}

	char * date_string = NULL;
	err = ocppj_get_string_field(payload, "retrieveDate", true, &date_string, err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_ERROR){
		ESP_LOGW(TAG, "Unable to get 'retrieveDate' from payload: %s", err_str);
		goto error;
	}

	update_info.retrieve_date = ocpp_parse_date_time(date_string);
	if(update_info.retrieve_date == (time_t)-1){
		ESP_LOGE(TAG, "Unable parse 'retriveDate'");

		snprintf(err_str, sizeof(err_str), "Unrecognised dateTime format for 'retriveDate'");
		err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;

		goto error;
	}

	err = ocppj_get_int_field(payload, "retryInterval", false, &update_info.retry_interval, err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_VALUE){
		if(err != eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Unable to get 'retryInterval' from payload: %s", err_str);
			goto error;
		}
	}else{
		update_info.retry_interval = 30;
	}

	int save_err = save_update_request(&update_info);
	if(save_err != 0){
		snprintf(err_str, sizeof(err_str), "Unable to save request: %s", strerror(save_err));

		err = eOCPPJ_ERROR_INTERNAL;
		ESP_LOGE(TAG, "%s", err_str);

		goto error;
	}

	if(defer_update(update_info.retrieve_date) != 0){
		snprintf(err_str, sizeof(err_str), "Unable to defer update");

		err = eOCPPJ_ERROR_INTERNAL;
		ESP_LOGE(TAG, "%s", err_str);

		goto error;
	}

	cJSON * reply =  ocpp_create_update_firmware_confirmation(unique_id);
	if(reply == NULL){
		ESP_LOGE(TAG, "Unable to create confirmation for update firmware");
	}else{
		send_call_reply(reply);
	}

	return;

error:
	if(err == eOCPPJ_NO_ERROR || eOCPPJ_NO_VALUE){
		ESP_LOGE(TAG, "Update firmware callback exit error without id");

		err = eOCPPJ_ERROR_INTERNAL;
		snprintf(err_str, sizeof(err_str), "Unknown error occured");
	}

	cJSON * error_reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(error_reply == NULL){
		ESP_LOGE(TAG, "Unable to create error reply");
	}else{
		send_call_reply(error_reply);
	}
}

int set_firmware_update_state(){
	ESP_LOGI(TAG, "Setting firmware update state");
	int err = load_update_request(&update_info);
	if(err != 0){
		return err != ENOENT ? err : 0; // No file, means no update was requested and no update related error.
	}

	if(update_info.retrieve_date < time(NULL)){ // An update should have been attempted
		cJSON * call = NULL;
		if(ota_CheckIfHasBeenUpdated()){
			call = ocpp_create_firmware_status_notification_request(OCPP_FIRMWARE_STATUS_INSTALLED);

			remove(firmware_update_request_path);
		}else{
			call = ocpp_create_firmware_status_notification_request(OCPP_FIRMWARE_STATUS_INSTALLATION_FAILED);

			if(update_info.retries > 0){
				update_info.retries--;
				err = save_update_request(&update_info);
				if(err != 0){
					return err;
				}else{
					/*
					 * Interval may exceed user expectation if reboot was not due to failed OTA, as the interval between last
					 * update and reboot/boot has been lost.
					 */
					err = defer_update(time(NULL)+update_info.retry_interval);
				}
			}else{
				remove(firmware_update_request_path);
			}
		}

		if(call != NULL){
			if(enqueue_call(call, NULL, NULL, NULL, eOCPP_CALL_GENERIC) != 0){
				ESP_LOGE(TAG, "Unable to enqueue firmware_status_notification");
				cJSON_Delete(call);
			}
		}else{
			ESP_LOGE(TAG, "Expected firmware status call");
		}

	}else{ // Update has been requested but no update should have been attempted.
		defer_update(update_info.retrieve_date);
	}

	return err;
}

uint8_t previous_enqueue_mask = 0;

time_t last_online_timestamp = 0;

static void transition_online(){
	ESP_LOGW(TAG, "Restoring previous mask: %d", previous_enqueue_mask);
	block_enqueue_call(previous_enqueue_mask);

	if(sessionHandler_OcppStateHasChanged()){
		sessionHandler_OcppSendState();
	}

	connected = true;
}

static void transition_offline(){
	last_online_timestamp = time(NULL);

	previous_enqueue_mask = get_blocked_enqueue_mask();
	ESP_LOGW(TAG, "Blocking generic and transaction messages. Storing current mask: %d", previous_enqueue_mask);
	block_enqueue_call(eOCPP_CALL_GENERIC | eOCPP_CALL_TRANSACTION_RELATED);

	sessionHandler_OcppSaveState();

	connected = false;
}

#define MAIN_EVENT_OFFSET 0
#define MAIN_EVENT_MASK 0xf
#define WEBSOCKET_EVENT_OFFSET 4
#define WEBSOCKET_EVENT_MASK 0xf0
#define TASK_EVENT_OFFSET 8
#define TASK_EVENT_MASK 0xf00

#define eOCPP_NO_EVENT 0

static void ocpp_task(){
	while(should_run){
		ESP_LOGI(TAG, "Attempting to start ocpp task");
		// TODO: see if there is a better way to check connectivity
		while(connectivity_GetActivateInterface() == eCONNECTION_NONE){
			if(should_run == false || should_restart)
				goto clean;

			ESP_LOGI(TAG, "Waiting for connection...");
			vTaskDelay(pdMS_TO_TICKS(2000));
		}

		//Indicate features that are not supported
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
		attach_call_cb(eOCPP_ACTION_TRIGGER_MESSAGE_ID, trigger_message_cb, NULL);

		attach_call_cb(eOCPP_ACTION_GET_DIAGNOSTICS_ID, get_diagnostics_cb, NULL);
		attach_call_cb(eOCPP_ACTION_UPDATE_FIRMWARE_ID, update_firmware_cb, NULL);

		ESP_LOGI(TAG, "Starting connection with Central System");

		int err = -1;
		unsigned int retry_attempts = 0;
		unsigned int retry_delay = 5;
		do{
			if(should_run == false || should_restart)
				goto clean;

			err = start_ocpp(storage_Get_url_ocpp(),
					i2cGetLoadedDeviceInfo().serialNumber,
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

		ocpp_configure_task_notification(task_ocpp_handle, TASK_EVENT_OFFSET);
		ocpp_configure_websocket_notification(task_ocpp_handle, WEBSOCKET_EVENT_OFFSET);

		connected = true;

		retry_attempts = 0;
		retry_delay = 5;
		do{

			if(should_run == false || should_restart)
				goto clean;

			err = complete_boot_notification_process(NULL, "Go", i2cGetLoadedDeviceInfo().serialNumber,
								"zaptec", GetSoftwareVersion(),
								LTEGetIccid(), LTEGetImsi(), NULL, NULL);
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

		ocpp_change_message_timeout(storage_Get_ocpp_message_timeout());
		start_ocpp_heartbeat();

		ocpp_set_on_new_period_cb(sessionHandler_OcppSetChargingVariables);
		if(ocpp_smart_charging_init() != ESP_OK){
			ESP_LOGE(TAG, "Unable to initiate smart charging");
		}

		//Handle ClockAlignedDataInterval
		restart_clock_aligned_meter_values();

		// Check if UpdateFormware.req is in progress or need to be restarted and update with FirmwareStatusNotification if needed
		err = set_firmware_update_state();
		if(err != 0){
			ESP_LOGE(TAG, "Error while attemptig to set firmware update status: %s", strerror(err));
		}

		unsigned int problem_count = 0;
		time_t last_problem_timestamp = time(NULL);
		uint enqueued_calls = 0;

		while(should_run && should_restart == false){
			uint32_t data = 0;

			if(connected && enqueued_calls > 0){ // Prevent blocking when calls are waiting
				data = ulTaskNotifyTake(pdTRUE, 0);
			} else if(connected){
				data = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			}else{
				data = ulTaskNotifyTake(pdTRUE, OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT - (time(NULL) - last_online_timestamp));
			}

			const uint websocket_event = (data & WEBSOCKET_EVENT_MASK) >> WEBSOCKET_EVENT_OFFSET;
			const uint task_event = (data & TASK_EVENT_MASK) >> TASK_EVENT_OFFSET;
			const uint main_event = (data & MAIN_EVENT_MASK) >> MAIN_EVENT_OFFSET;

			if(main_event != eOCPP_NO_EVENT){
				ESP_LOGI(TAG, "Handling main event");

				if(data & eOCPP_QUIT){
					ESP_LOGE(TAG, "Quitting ocpp loop");
					break;
				}
				if(data & eOCPP_FIRMWARE_UPDATE){
					ESP_LOGI(TAG, "Attempting to start firmware update from ocpp");
					MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
					if(ret == MsgCommandAck)
					{
						ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");

						start_ocpp_ota(update_info.location);
					}
					else
					{
						ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");
					}
				}
			}

			if(websocket_event != eOCPP_NO_EVENT && websocket_event){
				ESP_LOGI(TAG, "Handling websocket event");

				if(websocket_event & eOCPP_WEBSOCKET_CONNECTION_CHANGED){

					if(ocpp_is_connected() && !connected){
						ESP_LOGI(TAG, "Websocket went online");
						transition_online();

					}else if(!ocpp_is_connected() && connected){
						ESP_LOGW(TAG, "Websocket went offline");
						transition_offline();
					}
				}

				if(websocket_event & eOCPP_WEBSOCKET_FAILURE){ // TODO: Get additional websocket errors
					ESP_LOGW(TAG, "Websocket FAILURE %d", ++problem_count);

					if(last_problem_timestamp + OCPP_PROBLEM_RESET_INTERVAL > time(NULL)){
						problem_count = 1;
					}

					last_problem_timestamp = time(NULL);
				}

				if(problem_count > OCPP_PROBLEMS_COUNT_BEFORE_RETRY)
					break;

			}

			if(enqueued_calls > 0 || task_event & eOCPP_TASK_CALL_ENQUEUED){
				if(connected){
					int remaining = handle_ocpp_call();
					if(remaining != -1){
						enqueued_calls = remaining;
					}else{
						ESP_LOGE(TAG, "Unable to handle ocpp call");
						problem_count++; //TODO: integrate with problem count restart
						enqueued_calls = 1; // We don't know how many remain, will try atleast one more send.
					}
				}else{
					enqueued_calls = 1; // There may be more calls, but that is irrelevant
				}
			}

			if(!connected && last_online_timestamp + OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT <= time(NULL)){
				ESP_LOGE(TAG, "%d seconds since OCPP was last online, attempting reboot", OCPP_MAX_SEC_OFFLINE_BEFORE_REBOOT);
				esp_restart(); // TODO: write reason for reboot;
			}

		}
clean:
		ESP_LOGW(TAG, "Exited ocpp handling, tearing down");

		stop_ocpp_heartbeat();

		if(graceful_exit){
			ESP_LOGI(TAG, "Attemting graceful exit");
			for(size_t i = 1; i <= CONFIG_OCPP_NUMBER_OF_CONNECTORS; i++){
				ESP_LOGI(TAG, "Checking connector %d", i);
				if(sessionHandler_OcppTransactionIsActive(i)){
					ESP_LOGI(TAG, "Active transaction on connector %d", i);
					if(pending_reset){
						sessionHandler_OcppStopTransaction(OCPP_REASON_SOFT_RESET);
					}else{
						sessionHandler_OcppStopTransaction(OCPP_REASON_OTHER);
					}
					ESP_LOGI(TAG, "Transaction stopped");
				}
			}

			vTaskDelay(pdMS_TO_TICKS(1000)); // Allow time for session loop to transition away from active transaction

			ESP_LOGI(TAG, "Blocking non transaction related messages");
			block_enqueue_call(eOCPP_CALL_GENERIC | eOCPP_CALL_TRANSACTION_RELATED | eOCPP_CALL_BLOCKING);
			block_sending_call(eOCPP_CALL_GENERIC);

			time_t exit_start = time(NULL);
			uint exit_duration = 0;
			size_t message_count = enqueued_call_count();

			while(message_count > 0 && exit_duration < OCPP_EXIT_TIMEOUT){
				ESP_LOGW(TAG, "Remaining messages to send: %d. timeout: (%d/%d sec)",
					message_count, exit_duration, OCPP_EXIT_TIMEOUT);

				err = handle_ocpp_call();
				if(err < 0){
					ESP_LOGE(TAG, "Error sending message during graceful exit. Exiting non gracefully");
					break;
				}

				message_count = enqueued_call_count();
				exit_duration = time(NULL) - exit_start;
			}

			message_count = enqueued_call_count();
			exit_duration = time(NULL) - exit_start;

			if(message_count != 0){
				ESP_LOGE(TAG, "Non gracefull exit with %d abandoned messages after %d seconds",
					message_count, exit_duration);
			}
		}
		stop_ocpp();

		ESP_LOGI(TAG, "Teardown complete");
		should_restart = false;
	}

	if(pending_reset){
		ESP_LOGI(TAG, "Resetting due to reset request");
		reset();
	}

	task_ocpp_handle = NULL;
	vTaskDelete(NULL);
}

int ocpp_get_stack_watermark(){
	if(task_ocpp_handle != NULL){
		return uxTaskGetStackHighWaterMark(task_ocpp_handle);
	}else{
		return -1;
	}
}

bool ocpp_is_running(){
	return should_run;
}

bool ocpp_task_exists(){
	return (task_ocpp_handle != NULL);
}

void ocpp_end(bool graceful){
	should_run = false;
	xTaskNotify(task_ocpp_handle, eOCPP_QUIT, eSetBits);
	graceful_exit = graceful;
}

void ocpp_restart(bool graceful){
	should_restart = true;
	xTaskNotify(task_ocpp_handle, eOCPP_QUIT, eSetBits);
	graceful_exit = graceful;
}

void ocpp_init(){

	if(task_ocpp_handle == NULL){ // TODO: Make thread safe. NOTE: eTaskGetState returns eReady for deleted task
		should_run = true;
		task_ocpp_handle = xTaskCreateStatic(ocpp_task, "ocpp_task", TASK_OCPP_STACK_SIZE, NULL, 2, task_ocpp_stack, &task_ocpp_buffer);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
