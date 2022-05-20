#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "connectivity.h"
#include "i2cDevices.h"
#include "storage.h"

#include "ocpp_listener.h"
#include "ocpp_task.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "ocpp_json/ocppj_message_structure.h"
#include "types/ocpp_reset_status.h"
#include "types/ocpp_reset_type.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_key_value.h"
#include "types/ocpp_configuration_status.h"
#include "types/ocpp_meter_value.h"

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

static int set_config_u8(void (*config_function)(uint8_t), const char * value){
	char * endptr;
	long value_long = strtol(value, &endptr, 0);

	if(endptr[0] != '\0')
		return -1;

	if(value_long < 0 || value_long > UINT8_MAX)
		return -1;

	config_function((uint8_t)value_long);
	return 0;
}

static int set_config_u16(void (*config_function)(uint16_t), const char * value){
	char * endptr;
	long value_long = strtol(value, &endptr, 0);

	if(endptr[0] != '\0')
		return -1;

	if(value_long < 0 || value_long > UINT16_MAX)
		return -1;

	config_function((uint16_t)value_long);
	return 0;
}

static int set_config_u32(void (*config_function)(uint32_t), const char * value){
	char * endptr;
	long value_long = strtol(value, &endptr, 0);

	if(endptr[0] != '\0')
		return -1;

	if(value_long < 0 || value_long > UINT32_MAX)
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
	size_t prepared_index = 0;
	for(size_t i = 0; i < len+1; i++){
		if(!isspace(value[i])){
			if(iscntrl(value[i])){
				if(value[i]== '\0'){
					value_prepared[prepared_index++] = value[i];
					break;
				}else{
					goto error; // Dont trust input with unexpected control characters
				}
			}
			value_prepared[prepared_index++] = value[i];
		}
	}

	//Check if given configuration was only space
	if(strlen(value_prepared) == 0)
		goto error;

	size_t item_count = 1;

	// Check if number of items exceed max
	char * delimiter_ptr = value_prepared;
	for(size_t i = 0; i < max_items + 1; i++){
		delimiter_ptr = strchr(delimiter_ptr, ',');

		if(delimiter_ptr == NULL){
			break;
		}else{
			item_count++;
		}
	}

	if(item_count > max_items)
		goto error;

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
		if(!is_valid)
			goto error;

		token = strtok(NULL, ",");
	}
	config_function(value_prepared);

	free(value_prepared);
	return 0;

error:
	free(value_prepared);
	return -1;
}

static void change_configuration_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
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

	int err = -1;
	if(strcmp(key, OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS) == 0){
		err = set_config_bool(storage_Set_ocpp_authorize_remote_tx_requests, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_clock_aligned_data_interval, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_CONNECTION_TIMEOUT) == 0){
		err = set_config_u32(storage_Set_ocpp_connection_timeout, value);

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
		err = set_config_u32(storage_Set_ocpp_heartbeat_interval, value);

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
		err = set_config_csl(storage_Set_ocpp_meter_values_aligned_data, value, DEFAULT_CSL_SIZE, 22,
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
					OCPP_MEASURAND_TEMERATURE,
					OCPP_MEASURAND_VOLTAGE
				        );

		// TODO: "where applicable, the Measurand is combined with the optional phase; for instance: Voltage.L1"
	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_meter_values_sampled_data, value, DEFAULT_CSL_SIZE, 22,
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
					OCPP_MEASURAND_TEMERATURE,
					OCPP_MEASURAND_VOLTAGE
				        );

	}else if(strcmp(key, OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL) == 0){
		err = set_config_u32(storage_Set_ocpp_meter_value_sample_interval, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_RESET_RETRIES) == 0){
		err = set_config_u8(storage_Set_ocpp_reset_retries, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT) == 0){
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_ev_side_disconnect, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID) == 0){
		err = set_config_bool(storage_Set_ocpp_stop_transaction_on_invalid_id, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_aligned_data, value, DEFAULT_CSL_SIZE, 22,
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
					OCPP_MEASURAND_TEMERATURE,
					OCPP_MEASURAND_VOLTAGE
				        );

	}else if(strcmp(key, OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA) == 0){
		err = set_config_csl(storage_Set_ocpp_stop_txn_sampled_data, value, DEFAULT_CSL_SIZE, 22,
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
					OCPP_MEASURAND_TEMERATURE,
					OCPP_MEASURAND_VOLTAGE
				        );

	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS) == 0){
		err = set_config_u8(storage_Set_ocpp_transaction_message_attempts, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL) == 0){
		err = set_config_u16(storage_Set_ocpp_transaction_message_retry_interval, value);

	}else if(strcmp(key, OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT) == 0){
		err = set_config_bool(storage_Set_ocpp_unlock_connector_on_ev_side_disconnect, value);

	}else if(is_configuration_key(key)){
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
		return;
	}else{
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_NOT_SUPPORTED);
		return;
	}

	if(err == 0){
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_ACCEPTED);
	}else{
		change_config_confirm(unique_id, OCPP_CONFIGURATION_STATUS_REJECTED);
	}

	return;
}

static void ocpp_task(){
	while(true){
		// TODO: see if there is a better way to check connectivity
		while(!connectivity_GetActivateInterface() != eCONNECTION_NONE){
			ESP_LOGI(TAG, "Waiting for connection...");
			vTaskDelay(pdMS_TO_TICKS(2000));
		}
		ESP_LOGI(TAG, "Starting connection with Central System");

		int err = -1;
		unsigned int retry_attempts = 0;
		unsigned int retry_delay = 5;
		do{
			err = start_ocpp(i2cGetLoadedDeviceInfo().serialNumber);
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

		//Handle ocpp related configurations
		attach_call_cb(eOCPP_ACTION_GET_CONFIGURATION_ID, get_configuration_cb, NULL);
		attach_call_cb(eOCPP_ACTION_CHANGE_CONFIGURATION_ID, change_configuration_cb, NULL);

		//Handle features that are not bether handled by other components
		attach_call_cb(eOCPP_ACTION_RESET_ID, reset_cb, NULL);


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
				handle_ocpp_call();
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
