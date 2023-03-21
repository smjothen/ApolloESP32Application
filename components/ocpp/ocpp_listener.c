#include <stdio.h>
#include "cJSON.h"

#include "esp_log.h"

#include "ocpp_listener.h"
#include "ocpp_task.h"
#include "ocpp_call_with_cb.h"
#include "ocpp_json/ocppj_message_structure.h"
#include "messages/error_messages/ocpp_call_error.h"

static const char * TAG = "OCPP_LISTENER";
static TaskHandle_t task_to_notify = NULL;
static uint notify_offset = 0;

static const size_t MAX_DYNAMIC_BUFFER_SIZE = 32768;
static char * dynamic_buffer = NULL; // Used if payload is larger than websocket rx_buffer
static bool dynamic_failed = false;

struct ocpp_call_callback_with_data{
	ocpp_call_callback cb;
	void * cb_data;
};

static bool is_connected = false;

static struct ocpp_call_callback_with_data callbacks[OCPP_CALL_ACTION_ID_COUNT] = {0};

bool ocpp_is_connected(){
	return is_connected;
}

void clean_listener(){
	task_to_notify = NULL;
	notify_offset = 0;

	if(dynamic_buffer != NULL){
		free(dynamic_buffer);
		dynamic_buffer = NULL;
		dynamic_failed = true;
	}
}

void ocpp_configure_websocket_notification(TaskHandle_t task, uint offset){
	task_to_notify = task;
	notify_offset = offset;
}

int attach_call_cb(enum ocpp_call_action_id action_id, ocpp_call_callback call_cb, void * cb_data){

	if(callbacks[action_id].cb != NULL){
		ESP_LOGE(TAG, "Unable to attach callback, other callback exists");
		return -1;
	}

	callbacks[action_id].cb = call_cb;
	callbacks[action_id].cb_data = cb_data;

	return 0;
}

static int ocpp_parse_message(cJSON * ocpp_call, int * message_type_id_out, char ** unique_id_out, char ** action_out, cJSON ** payload, char ** error_code_out, char ** error_description_out, cJSON ** error_details){
	if(cJSON_IsArray(ocpp_call)){
		*message_type_id_out = cJSON_GetArrayItem(ocpp_call, 0)->valueint;
		*unique_id_out = cJSON_GetArrayItem(ocpp_call, 1)->valuestring;

		switch(*message_type_id_out){
		case eOCPPJ_MESSAGE_ID_CALL:
			*action_out = cJSON_GetArrayItem(ocpp_call, 2)->valuestring;
			*payload = cJSON_GetArrayItem(ocpp_call, 3);
			break;
		case eOCPPJ_MESSAGE_ID_RESULT:
			*payload = cJSON_GetArrayItem(ocpp_call, 2);
			break;
		case eOCPPJ_MESSAGE_ID_ERROR:
			*error_code_out = cJSON_GetArrayItem(ocpp_call, 2)->valuestring;
			*error_description_out = cJSON_GetArrayItem(ocpp_call, 3)->valuestring;
			*error_details = cJSON_GetArrayItem(ocpp_call, 4);
			break;
		default:
			ESP_LOGE(TAG, "MessageTypeId is invalid");
			return -1;
		}
	}else{
		ESP_LOGE(TAG, "Unexpected call structure");
		return -1;
	}

	return 0;
}

int check_call_validity(const char * action){
	switch(get_registration_status()){
	case eOCPP_REGISTRATION_ACCEPTED:
		return 0;
	case eOCPP_REGISTRATION_PENDING:
		// "While in pending state, the following Central System initiated messages are not allowed:
		// RemoteStartTransaction.req and RemoteStopTransaction.req"
		if(strcmp(action, OCPPJ_ACTION_START_TRANSACTION) == 0 || strcmp(action, OCPPJ_ACTION_STOP_TRANSACTION) == 0)
			return -1;
		break;
	case eOCPP_REGISTRATION_REJECTED:
		//"While Rejected, the Charge Point SHALL NOT respond to any Central System initiated message."
		return -1;
	}
	return 0;
}

enum ocpp_call_action_id action_id_from_action(const char * action){
	if(strcmp(action, OCPPJ_ACTION_CANCEL_RESERVATION) == 0){
		return eOCPP_ACTION_CANCEL_RESERVATION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_CHANGE_AVAILABILITY) == 0){
		return eOCPP_ACTION_CHANGE_AVAILABILITY_ID;
	}else if(strcmp(action, OCPPJ_ACTION_CHANGE_CONFIGURATION) == 0){
		return eOCPP_ACTION_CHANGE_CONFIGURATION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_CLEAR_CACHE) == 0){
		return eOCPP_ACTION_CLEAR_CACHE_ID;
	}else if(strcmp(action, OCPPJ_ACTION_CLEAR_CHARGING_PROFILE) == 0){
		return eOCPP_ACTION_CLEAR_CHARGING_PROFILE_ID;
	}else if(strcmp(action, OCPPJ_ACTION_DATA_TRANSFER) == 0){
		return eOCPP_ACTION_DATA_TRANSFER_ID;
	}else if(strcmp(action, OCPPJ_ACTION_GET_COMPOSITE_SCHEDULE) == 0){
		return eOCPP_ACTION_GET_COMPOSITE_SCHEDULE_ID;
	}else if(strcmp(action, OCPPJ_ACTION_GET_CONFIGURATION) == 0){
		return eOCPP_ACTION_GET_CONFIGURATION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_GET_DIAGNOSTICS) == 0){
		return eOCPP_ACTION_GET_DIAGNOSTICS_ID;
	}else if(strcmp(action, OCPPJ_ACTION_GET_LOCAL_LIST_VERSION) == 0){
		return eOCPP_ACTION_GET_LOCAL_LIST_VERSION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_REMOTE_START_TRANSACTION) == 0){
		return eOCPP_ACTION_REMOTE_START_TRANSACTION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_REMOTE_STOP_TRANSACTION) == 0){
		return eOCPP_ACTION_REMOTE_STOP_TRANSACTION_ID;
	}else if(strcmp(action, OCPPJ_ACTION_RESERVE_NOW) == 0){
		return eOCPP_ACTION_RESERVE_NOW_ID;
	}else if(strcmp(action, OCPPJ_ACTION_RESET) == 0){
		return eOCPP_ACTION_RESET_ID;
	}else if(strcmp(action, OCPPJ_ACTION_SEND_LOCAL_LIST) == 0){
		return eOCPP_ACTION_SEND_LOCAL_LIST_ID;
	}else if(strcmp(action, OCPPJ_ACTION_SET_CHARGING_PROFILE) == 0){
		return eOCPP_ACTION_SET_CHARGING_PROFILE_ID;
	}else if(strcmp(action, OCPPJ_ACTION_TRIGGER_MESSAGE) == 0){
		return eOCPP_ACTION_TRIGGER_MESSAGE_ID;
	}else if(strcmp(action, OCPPJ_ACTION_UNLOCK_CONNECTOR) == 0){
		return eOCPP_ACTION_UNLOCK_CONNECTOR_ID;
	}else if(strcmp(action, OCPPJ_ACTION_UPDATE_FIRMWARE) == 0){
		return eOCPP_ACTION_UPDATE_FIRMWARE_ID;
	}else{
		return -1;
	}
}

int call_handler(esp_websocket_client_handle_t client, const char * unique_id, const char * action, cJSON * payload){

	if(action == NULL){
		ESP_LOGE(TAG, "Expected action, got NULL");
		return -1;
	}

	if(check_call_validity(action)){
		ESP_LOGE(TAG, "Ignoring '%s' due to incompatible state", action);
		return -1;
	}

	enum ocpp_call_action_id action_id = action_id_from_action(action);
	if(action_id == -1){
		ESP_LOGE(TAG, "Unable to associate action string with ocpp action: %s", action);
	}

	if(action_id != -1 && callbacks[action_id].cb != NULL){
		callbacks[action_id].cb(unique_id, action, payload, callbacks[action_id].cb_data);
		return 0;
	}
	else{
		ESP_LOGE(TAG, "Call to action with missing handler: '%s'", action);
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_NOT_IMPLEMENTED, "", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create response for missing action");
			return -1;
		}
		send_call_reply(ocpp_error);
		return -1;
	}
}

void text_frame_handler(esp_websocket_client_handle_t client, const char * data){
	cJSON * ocpp_request = cJSON_Parse(data);

	if(ocpp_request == NULL){
		ESP_LOGE(TAG, "Unable to parse text frame");
		return;
	}

	int message_type_id;
	char * unique_id;
	char * action;
	char * error_code;
	char * error_description;
	cJSON * payload;
	cJSON * error_details;

	int err = ocpp_parse_message(ocpp_request, &message_type_id, &unique_id, &action, &payload, &error_code, &error_description, &error_details);

	if(err != 0){
		ESP_LOGE(TAG, "Unable to handle text frame");
		cJSON_Delete(ocpp_request);
		return;
	}

	struct ocpp_active_call call = {0};

	switch(message_type_id){
	case eOCPPJ_MESSAGE_ID_CALL:
		ESP_LOGD(TAG, "Recieved ocpp call message");
		call_handler(client, unique_id, action, payload);
		break;

	case eOCPPJ_MESSAGE_ID_RESULT:
		ESP_LOGD(TAG, "Recieved ocpp result message: %s", unique_id);

		if(take_active_call_if_match(&call, unique_id, 500) != pdTRUE){
			ESP_LOGE(TAG, "Unable to match result to an active call");
			break;
		}

		if(task_to_notify != NULL)
			xTaskNotify(task_to_notify, eOCPP_WEBSOCKET_RECEIVED_MATCHING<<notify_offset, eSetBits);

		if(call.call->result_cb != NULL){
			call.call->result_cb(unique_id, payload, call.call->cb_data);
		}else{
			result_logger(unique_id, payload, call.call->cb_data);
		}

		free_call_with_cb(call.call);
		break;

	case eOCPPJ_MESSAGE_ID_ERROR:
		ESP_LOGE(TAG, "Recieved ocpp error message");

		if(take_active_call_if_match(&call, unique_id, 500) != pdTRUE){
			ESP_LOGE(TAG, "Unable to match error response to an active call");
			break;
		}

		if(task_to_notify != NULL)
			xTaskNotify(task_to_notify, eOCPP_WEBSOCKET_RECEIVED_MATCHING<<notify_offset, eSetBits);

		fail_active_call(&call, error_code, error_description, error_details);
		break;
	}

	cJSON_Delete(ocpp_request);
}

void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
	esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
	esp_websocket_client_handle_t client = (esp_websocket_client_handle_t)handler_args;

	switch (event_id) {
	case WEBSOCKET_EVENT_CONNECTED:
		ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
		if(is_connected == false){
			is_connected = true;

			if(task_to_notify != NULL)
				xTaskNotify(task_to_notify, eOCPP_WEBSOCKET_CONNECTION_CHANGED<<notify_offset, eSetBits);
		}
		break;
	case WEBSOCKET_EVENT_DISCONNECTED:
		ESP_LOGW(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
		if(is_connected == true){
			is_connected = false;

			if(task_to_notify != NULL)
				xTaskNotify(task_to_notify, eOCPP_WEBSOCKET_CONNECTION_CHANGED<<notify_offset, eSetBits);
		}
		break;
	case WEBSOCKET_EVENT_DATA:
		switch(data->op_code){
		case 0:
			ESP_LOGE(TAG, "Got unexpected continuation frame");
			break;
		case 1:
			ESP_LOGD(TAG, "Handle text frame");

			if(data->payload_len > data->data_len){ // If payload exceed websocket rx_buffer
				if(data->payload_len > MAX_DYNAMIC_BUFFER_SIZE){
					ESP_LOGE(TAG, "Unable to handle websocket request: request size too large: %d", data->payload_len);
					break;
				}

				if(data->payload_offset == 0){
					ESP_LOGW(TAG, "Allocating buffer for unusually large websocket request. Size %d", data->payload_len);
					if(dynamic_buffer != NULL)
						free(dynamic_buffer);

					dynamic_buffer = malloc(sizeof(char) * (data->payload_len + 1));
					if(dynamic_buffer == NULL){
						ESP_LOGE(TAG, "Unable to allocate additional websocket buffer");
						dynamic_failed = true;
						break;
					}

					ESP_LOGI(TAG, "Allocation successfull");
					dynamic_failed = false;
				}

				if(!dynamic_failed){
					ESP_LOGI(TAG, "Copying to allocated buffer | from %d to %d | size: %d", data->payload_offset, data->payload_offset + data->data_len, data->payload_len);
					memcpy(dynamic_buffer + data->payload_offset, data->data_ptr, data->data_len);

					if(data->payload_offset + data->data_len == data->payload_len){
						dynamic_buffer[data->payload_len] = '\0';

						ESP_LOGI(TAG, "Completed buffer, executing request");
						text_frame_handler(client, dynamic_buffer);

						free(dynamic_buffer);
						dynamic_buffer = NULL;
						dynamic_failed = true;
					}
				}
				break;
			}

			text_frame_handler(client, data->data_ptr);
			break;
		case 2:
			ESP_LOGE(TAG, "Got unexpected binary frame");
			break;
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			ESP_LOGE(TAG, "Recieved reserved opcode");
			break;
		case 8:
			ESP_LOGW(TAG, "Recieved connection closed");
			break;
		case 9:
			ESP_LOGD(TAG, "Recieved ping");
			break;
		case 10:
			ESP_LOGD(TAG, "Recieved pong");
			break;
		}

		break;
	case WEBSOCKET_EVENT_ERROR:
		ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
		if(task_to_notify != NULL)
			xTaskNotify(task_to_notify, eOCPP_WEBSOCKET_FAILURE<<notify_offset, eSetBits);
		break;
	}
}
