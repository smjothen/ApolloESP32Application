#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "connectivity.h"
#include "i2cDevices.h"

#include "ocpp_listener.h"
#include "ocpp_task.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "ocpp_json/ocppj_message_structure.h"
#include "types/ocpp_reset_status.h"
#include "types/ocpp_reset_type.h"

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
