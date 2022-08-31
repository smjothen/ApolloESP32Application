#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "cJSON.h"

#include "ocpp_task.h"
#include "ocpp_listener.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_date_time.h"

static const char *TAG = "OCPP_TASK";

// TODO: Tune, currently above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
// to prevent use of DMA. esp_websocket_client allocates this twice (for rx_buffer and tx_buffer)
#define WEBSOCKET_BUFFER_SIZE 2048

esp_websocket_client_handle_t client;

SemaphoreHandle_t ocpp_active_call_lock_1 = NULL;

/**
 * The specification allows only one active call from the charge point:
 * "A Charge Point or Central System SHOULD NOT send a CALL message to the other party unless all the
 * CALL messages it sent before have been responded to or have timed out."
 */
char * active_call_id; // "Maximum of 36 characters, to allow for GUIDs" (37 with '\0')
struct ocpp_call_with_cb * active_call = NULL;
bool is_trigger_message = false; // Will always be false until TriggerMessage is implemented

time_t last_call_timestamp = {0};

QueueHandle_t ocpp_call_queue = NULL; // For normal messages
QueueHandle_t ocpp_blocking_call_queue = NULL; // For messages that prevent significant ocpp behaviour (BootNotification)
QueueHandle_t ocpp_transaction_call_queue = NULL; // transactions SHOULD be delivered as soon as possible, in chronological order, MUST queue when offline

bool websocket_connected = false;
enum ocpp_registration_status registration_status = eOCPP_REGISTRATION_PENDING;

uint32_t default_heartbeat_interval = 60 * 60 * 24;
long heartbeat_interval = -1;
TimerHandle_t heartbeat_handle = NULL;

#define OCPP_TIME_SYNC_MARGIN 5
#define OCPP_MAX_EXPECTED_OFFSET 60*60 // 1 hour
#define MAX_HEARTBEAT_INTERVAL UINT32_MAX
#define MINIMUM_HEARTBEAT_INTERVAL 1 // To prevent flooding the central service with heartbeat
#define WEBSOCKET_WRITE_TIMEOUT 5000

#define MAX_TRANSACTION_QUEUE_SIZE 20

static uint8_t transaction_message_attempts = 3;
static uint8_t transaction_message_retry_interval = 60;

char last_transaction_id[37] = {0};
struct ocpp_call_with_cb * failed_transaction = NULL;
unsigned int failed_transaction_count = 0;

time_t last_transaction_timestamp = {0};

long int central_system_time_offset = 0;

time_t (*non_enqueued_timestamp)();
cJSON * (*non_enqueued_message)(void ** cb_data);

bool offline_enabled = false;
ocpp_result_callback start_result_cb;
ocpp_error_callback start_error_cb;

ocpp_result_callback stop_result_cb;
ocpp_error_callback stop_error_cb;

ocpp_result_callback meter_result_cb;
ocpp_error_callback meter_error_cb;

void ocpp_set_offline_functions(time_t (*oldest_non_enqueued_timestamp)(), cJSON * (*oldest_non_enqueued_message)(void ** cb_data),
			ocpp_result_callback start_transaction_result_cb, ocpp_error_callback start_transaction_error_cb, void * start_transaction_cb_data,
			ocpp_result_callback stop_transaction_result_cb, ocpp_error_callback stop_transaction_errror_cb, void * stop_transaction_cb_data,
			ocpp_result_callback meter_transaction_result_cb, ocpp_error_callback meter_transaction_error_cb, void * meter_transaction_cb_data){

	non_enqueued_timestamp = oldest_non_enqueued_timestamp;
	non_enqueued_message = oldest_non_enqueued_message;

	start_result_cb = start_transaction_result_cb;
	start_error_cb = start_transaction_error_cb;

	stop_result_cb = stop_transaction_result_cb;
	stop_error_cb = stop_transaction_errror_cb;

	meter_result_cb = meter_transaction_result_cb;
	meter_error_cb = meter_transaction_error_cb;

	offline_enabled = true;
}


enum ocpp_registration_status get_registration_status(){
	return registration_status;
}

bool is_connected(void){
	return websocket_connected;
}

void set_connected(bool connected){
	websocket_connected = connected;
}

void update_central_system_time_offset(time_t charge_point_time, time_t central_system_time){
	time_t offset = central_system_time - (charge_point_time + central_system_time_offset);

	if(abs(offset) > OCPP_TIME_SYNC_MARGIN){
		if(abs(offset) > OCPP_MAX_EXPECTED_OFFSET){
			ESP_LOGW(TAG, "Time difference between charge point and central system is unexpectedly large %ld", offset);
		}
		central_system_time_offset += offset;
		ESP_LOGI(TAG, "Updating central system time syncronization to %ld relative to charge point time", central_system_time_offset);
	}else{
		ESP_LOGI(TAG, "Time difference between syncronized charge point and central system is within margins");
	}

	//TODO: Check if settimeofday() or adjtime() should be used
}

// The related functions do not verify if get/set overwrites front with back or back with front
struct timestamp_queue{
	time_t timestamps[MAX_TRANSACTION_QUEUE_SIZE];
	int front;
	int back;
};

static struct timestamp_queue txn_enqueue_timestamps = {
	.front = -1,
	.back = -1,
};

static void set_txn_enqueue_timestamp(time_t timestamp){
	txn_enqueue_timestamps.back++;
	if(txn_enqueue_timestamps.back == MAX_TRANSACTION_QUEUE_SIZE)
		txn_enqueue_timestamps.back = 0;

	txn_enqueue_timestamps.timestamps[txn_enqueue_timestamps.back] = timestamp;
}

static time_t get_txn_enqueue_timestamp(){
	txn_enqueue_timestamps.front++;
	if(txn_enqueue_timestamps.front == MAX_TRANSACTION_QUEUE_SIZE)
		txn_enqueue_timestamps.front = 0;

	return txn_enqueue_timestamps.timestamps[txn_enqueue_timestamps.front];
}

static time_t peek_txn_enqueue_timestamp(){
	if(txn_enqueue_timestamps.front == txn_enqueue_timestamps.back) // No data to peek or get/set missused
		return LONG_MAX;

	int position = txn_enqueue_timestamps.front +1;
	if(position == MAX_TRANSACTION_QUEUE_SIZE)
		position = 0;

	return txn_enqueue_timestamps.timestamps[position];
}

/*
 * Stores ttl, CP generated id and CS generated id
 * ttl is given as the number of conversions attempted before id
 * becomes irrelevant.
 */
int transaction_id_conversion_table[MAX_TRANSACTION_QUEUE_SIZE * 3] = {0};
int last_index_in_use = 0;

int ocpp_update_enqueued_transaction_id(int old_id, int new_id){
	int ttl = uxQueueMessagesWaiting(ocpp_transaction_call_queue);
	if(ttl < 1)
		return 0;

	for(size_t i = 0; i < sizeof(transaction_id_conversion_table); i += 3){
		if(transaction_id_conversion_table[i] < 1){
			transaction_id_conversion_table[i] = ttl;
			transaction_id_conversion_table[i+1] = old_id;
			transaction_id_conversion_table[i+2] = new_id;

			if(i > last_index_in_use)
				last_index_in_use = i;
			return 0;
		}
	}

	return -1;
}

static bool convert_tmp_id(int tmp_id, int * result_id_out){
	bool found_conversion = false;
	size_t i;

	for(i = 0; i < last_index_in_use; i += 3){ // look for tmp id while lovering ttl
		if(transaction_id_conversion_table[i] > 0){
			if(transaction_id_conversion_table[i+1] == tmp_id){
				*result_id_out = transaction_id_conversion_table[i+2];
				found_conversion = true;
			}

			transaction_id_conversion_table[i]--;
		}
	}

	for(; i < last_index_in_use; i += 3){ // continue lovering ttl for all indexes in use
		if(transaction_id_conversion_table[i] > 0)
			transaction_id_conversion_table[i]--;
	}

	for(i = last_index_in_use; i > 0; i -= 3){ // update last index in use
		last_index_in_use = i;

		if(transaction_id_conversion_table[i] > 0){
			break;
		}
	}

	return found_conversion;
}

int add_call(struct ocpp_call_with_cb * message, enum call_type type){
	if(ocpp_call_queue == NULL)
		return -1;

	switch(type){
	case eOCPP_CALL_GENERIC:
		if(xQueueSendToBack(ocpp_call_queue, &message, pdMS_TO_TICKS(500)) != pdPASS){
			return -1;
		}
		break;
	case eOCPP_CALL_TRANSACTION_RELATED:
		if(xQueueSendToBack(ocpp_transaction_call_queue, &message, pdMS_TO_TICKS(500)) != pdPASS){
			return -1;
		}else{
			set_txn_enqueue_timestamp(time(NULL));
		}
		break;
	case eOCPP_CALL_BLOCKING:
		if(xQueueSendToBack(ocpp_blocking_call_queue, &message, pdMS_TO_TICKS(500)) != pdPASS){
			return -1;
		}
		break;
	default:
		ESP_LOGE(TAG, "Unable to add call: Invalid call_type");
		return -1;
	}
	return 0;
}

static uint8_t enqueue_blocking_mask = 0;

void block_enqueue_call(uint8_t call_type_mask){
	enqueue_blocking_mask = call_type_mask;
}

uint8_t get_blocked_enqueue_mask(){
	return enqueue_blocking_mask;
}

int enqueue_call(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type){
	if(call == NULL){
		ESP_LOGE(TAG, "Invalid call: NULL");
		return -1;
	}

	if(enqueue_blocking_mask & type){
		ESP_LOGW(TAG, "Enqueue is blocked by mask");
		return -1;
	}

	struct ocpp_call_with_cb * message_with_cb = malloc(sizeof(struct ocpp_call_with_cb));
	if(message_with_cb == NULL){
		ESP_LOGE(TAG, "Unable to allocate buffer for message callback struct");
		return -1;
	}

	message_with_cb->call_message = call;
	message_with_cb->result_cb = result_cb;
	message_with_cb->error_cb = error_cb;
	message_with_cb->cb_data = cb_data;

	int err = add_call(message_with_cb, type);

	if(err != 0){
		ESP_LOGE(TAG, "Unable to enqueue call");
		free(message_with_cb);
	}

	return err;
}

static uint8_t call_blocking_mask = 0;

void block_sending_call(uint8_t call_type_mask){
	call_blocking_mask = call_type_mask;
}

size_t enqueued_call_count(){
	size_t count = 0;

	if(!(call_blocking_mask & eOCPP_CALL_GENERIC) && ocpp_call_queue != NULL)
		count += uxQueueMessagesWaiting(ocpp_call_queue);

	if(!(call_blocking_mask & eOCPP_CALL_TRANSACTION_RELATED) && ocpp_transaction_call_queue != NULL)
		count += uxQueueMessagesWaiting(ocpp_transaction_call_queue);

	if(!(call_blocking_mask & eOCPP_CALL_BLOCKING) && ocpp_blocking_call_queue != NULL)
		count += uxQueueMessagesWaiting(ocpp_blocking_call_queue);

	return count;
}

static void reset_heartbeat_timer(void){
	if(heartbeat_handle != NULL){
		xTimerReset(heartbeat_handle, pdMS_TO_TICKS(100));
	}
}

int send_call_reply(cJSON * call){
	if(call == NULL){
		ESP_LOGE(TAG, "Invalid call reply: NULL");
		return -1;
	}

	char * message = cJSON_Print(call);
	if(message == NULL){
		ESP_LOGE(TAG, "Unable to create message string");
		return -1;
	}

	int err = esp_websocket_client_send_text(client, message, strlen(message), pdMS_TO_TICKS(WEBSOCKET_WRITE_TIMEOUT));
	free(message);

	if(err == -1){
		ESP_LOGE(TAG, "Error sending with websocket");
		return -1;
	}else{
		reset_heartbeat_timer();
		return 0;
	}
}

void ocpp_send_status_notification(enum ocpp_cp_status_id new_state, const char * error_code, const char * info){
	ESP_LOGD(TAG, "Sending status notification");

	char state[15];

	switch(new_state){
	case eOCPP_CP_STATUS_AVAILABLE:
		strcpy(state, OCPP_CP_STATUS_AVAILABLE);
		break;
	case eOCPP_CP_STATUS_PREPARING:
		strcpy(state, OCPP_CP_STATUS_PREPARING);
		break;
	case eOCPP_CP_STATUS_CHARGING:
		strcpy(state, OCPP_CP_STATUS_CHARGING);
		break;
	case eOCPP_CP_STATUS_SUSPENDED_EV:
		strcpy(state, OCPP_CP_STATUS_SUSPENDED_EV);
		break;
	case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		strcpy(state, OCPP_CP_STATUS_SUSPENDED_EVSE);
		break;
	case eOCPP_CP_STATUS_FINISHING:
		strcpy(state, OCPP_CP_STATUS_FINISHING);
		break;
	case eOCPP_CP_STATUS_RESERVED:
		strcpy(state, OCPP_CP_STATUS_RESERVED);
		break;
	case eOCPP_CP_STATUS_UNAVAILABLE:
		strcpy(state, OCPP_CP_STATUS_UNAVAILABLE);
		break;
	case eOCPP_CP_STATUS_FAULTED:
		strcpy(state, OCPP_CP_STATUS_FAULTED);
		break;
	default:
		ESP_LOGE(TAG, "Unknown status id: %d", new_state);
		return;
	}

	cJSON * status_notification  = ocpp_create_status_notification_request(1, error_code, info, state, time(NULL), NULL, NULL);
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


int check_send_legality(const char * action){

	switch(registration_status){
	case eOCPP_REGISTRATION_ACCEPTED:
		return 0;
	case eOCPP_REGISTRATION_PENDING:
		// "The Charge Point SHALL send a BootNotification.req PDU each time it boots or reboots. Between the physical
		// power-on/reboot and the successful completion of a BootNotification, where Central System returns Accepted or
		// Pending, the Charge Point SHALL NOT send any other request to the Central System."[...]
		// "[if BootNotification returns pending/rejected] the value of the interval field indicates the minimum wait time
		// before sending a next BootNotification request [...] unless requested to do so with a TriggerMessage.req[...]
		// The Charge Point SHALL NOT send request messages to the Central System unless it has been instructed
		// by the Central System to do so with a TriggerMessage.req request.
		if((strcmp(action, OCPPJ_ACTION_BOOT_NOTIFICATION) != 0) && is_trigger_message == false)
			return -1;

		if((strcmp(action, OCPPJ_ACTION_BOOT_NOTIFICATION) == 0) &&
			time(NULL) < last_call_timestamp + heartbeat_interval && is_trigger_message == false)
			return -1;
		break;
	case eOCPP_REGISTRATION_REJECTED:
		if(strcmp(action, OCPPJ_ACTION_BOOT_NOTIFICATION) != 0)
			return -1;

		if((strcmp(action, OCPPJ_ACTION_BOOT_NOTIFICATION) == 0) && time(NULL) < last_call_timestamp + heartbeat_interval)
			return -1;
		break;
	}

	return 0;
}

BaseType_t take_active_call(struct ocpp_call_with_cb ** call_data, char  ** call_id, uint timeout_ms){
	if(xSemaphoreTake(ocpp_active_call_lock_1, pdMS_TO_TICKS(timeout_ms)) == pdTRUE){
		*call_data = active_call;
		*call_id = active_call_id;

		return pdTRUE;
	}else{
		*call_data = NULL;
		call_id = NULL;

		return pdFALSE;
	}
}

BaseType_t give_active_call(){
	return xSemaphoreGive(ocpp_active_call_lock_1);
}

static bool is_transaction_related_message(const char * unique_id){
	return strncmp(unique_id, last_transaction_id, 36) == 0;
}

void clear_active_call(void){

	if(active_call != NULL){
		// If active call is a transaction retry, then also clear retry data
		if(failed_transaction_count > 0){
			if(cJSON_IsArray(active_call->call_message) && cJSON_GetArrayItem(active_call->call_message, 0)->valueint == eOCPPJ_MESSAGE_ID_CALL){
				const char * unique_id = cJSON_GetArrayItem(active_call->call_message, 1)->valuestring;

				if(is_transaction_related_message(unique_id)){
					failed_transaction_count = 0;
				}
			}
		}

		cJSON_Delete(active_call->call_message); //Includes reference of active_call_id
		active_call_id = NULL;
		free(active_call);
		active_call = NULL;
	}

	if(ocpp_active_call_lock_1 != NULL)
		xSemaphoreGive(ocpp_active_call_lock_1);
}

// Used if the active call was sendt or attempted to be sendt to allow retrying transaction related messages
void fail_active_call(const char * fail_description){
	if(active_call != NULL){
		if(cJSON_IsArray(active_call->call_message) && cJSON_GetArrayItem(active_call->call_message, 0)->valueint == eOCPPJ_MESSAGE_ID_CALL){
			const char * unique_id = cJSON_GetArrayItem(active_call->call_message, 1)->valuestring;

			if(is_transaction_related_message(unique_id)){
				if(failed_transaction_count < transaction_message_attempts){
					ESP_LOGW(TAG, "Preparing failed transaction for retry");
					active_call_id = NULL;

					failed_transaction = active_call;
					active_call = NULL;

					failed_transaction_count++;

					xSemaphoreGive(ocpp_active_call_lock_1);
				}
				else{
					ESP_LOGE(TAG, "Giving up on transaction: retry attemps exceeded");
					failed_transaction_count = 0;
					clear_active_call();
				}
			}
			else{
				active_call->error_cb(0, "CP failure", fail_description, NULL, active_call->cb_data);
				clear_active_call();
			}
		}else{
			ESP_LOGE(TAG, "Message is malformed, unable to determin type");
			active_call->error_cb(0, "CP failure", fail_description, NULL, active_call->cb_data);
			clear_active_call();
		}
	}
}

int send_next_call(int last_listener_state){

	struct ocpp_call_with_cb * call;
	if(ocpp_active_call_lock_1 == NULL){
		ESP_LOGE(TAG, "The lock is not initialized");
		return -1;
	}

	struct ocpp_call_with_cb * last_call;
	char * last_call_id;
	BaseType_t aquired_lock = pdFALSE;
	/*
	 * If we are not able to take the active call immediately, that means that ocpp_listener has recieved a result.
	 * We give it 5 seconds to finish to finish handeling the result.
	 */
	int new_listener_state = eOCPP_WEBSOCKET_NO_EVENT;
	if((aquired_lock = take_active_call(&last_call, &last_call_id, 5000)) == pdTRUE){
		//We check if ocpp_listener has informed us of a result and there is an active call
		if(last_call != NULL && last_listener_state != eOCPP_WEBSOCKET_RECEIVED_MATCHING){
			//The wait is 0 as ocpp_listener should only send this message while it has aquired the active call lock.
			new_listener_state = ulTaskNotifyTake(pdTRUE, 0);
		}else{
			// If there is no active call, then no response matches with no call
			new_listener_state = eOCPP_WEBSOCKET_RECEIVED_MATCHING;
		}

		if(new_listener_state == eOCPP_WEBSOCKET_RECEIVED_MATCHING){  // check if listener has received response
			if(last_call != NULL){

				ESP_LOGE(TAG, "ocpp_listener handled result but did not clear it!");

				clear_active_call(); // We clear the call that listener should have
				 // And reaquire the active_call_lock for next call
				aquired_lock = take_active_call(&last_call, &last_call_id, 500);
			}
			// The active call is now cleared and lock is ready for next call
		}else{
			/*
			 * else the ocpp_listener has not recieved a response. We give back the active call lock.
			 * and wait for a message timeout. We save the listener state if it indicate connection change
			 * so that we can return the newest connection state.
			 */
			give_active_call();

			time_t wait_begin = time(NULL);
			do{
				if(new_listener_state == eOCPP_WEBSOCKET_DISCONNECT
					|| new_listener_state == eOCPP_WEBSOCKET_CONNECTED){

					last_listener_state = new_listener_state;
				}

				time_t sec_to_timeout = OCPP_CALL_TIMEOUT - (time(NULL) - wait_begin);
				if(sec_to_timeout > 0){
					new_listener_state = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sec_to_timeout));
				}else{
					new_listener_state = eOCPP_WEBSOCKET_NO_EVENT;
				}

			}while(new_listener_state != eOCPP_WEBSOCKET_RECEIVED_MATCHING
				&& new_listener_state != eOCPP_WEBSOCKET_NO_EVENT);

			//Ìf ocpp_listener indicate response is matching, we give it 5 seconds to handle it and release the lock
			if(new_listener_state == eOCPP_WEBSOCKET_RECEIVED_MATCHING){
				if((aquired_lock = take_active_call(&last_call, &last_call_id, 5000)) != pdTRUE)
					ESP_LOGE(TAG, "ocpp_listener recieved result, but did not finish within 5 seconds");

			}else{
				ESP_LOGE(TAG, "Call timed out, clearing for next call");
				if(take_active_call(&last_call, &last_call_id, 500) == pdTRUE){
					fail_active_call("timeout");
				}else{
					ESP_LOGE(TAG, "Unable to aquire lock for clearing");
					return -1;
				}
				aquired_lock = take_active_call(&last_call, &last_call_id, 500);
			}

		}
	}else{
		/*
		 * If it is not able to handle the result in 5 seconds, we log it as an error because it might indicate a deadlock.
		 * We exit and try again next time send_next_call is called. If the listener is done by then there should be no problem.
		 */
		ESP_LOGE(TAG, "Unable to take active call. ocpp_listener might be causing a deadlock preventing new ocpp messages.");
		return -1;
	}

	if(aquired_lock != pdTRUE){
		ESP_LOGE(TAG, "Unable to aquire lock for sending new call");
		return -1;
	}

	BaseType_t call_aquired = pdFALSE;

	if(!(call_blocking_mask & eOCPP_CALL_BLOCKING)){ // Attempt to get call from prioritized queue
		call_aquired = xQueueReceive(ocpp_blocking_call_queue, &call, pdMS_TO_TICKS(1));
	}

	if(call_aquired != pdTRUE && !(call_blocking_mask & eOCPP_CALL_TRANSACTION_RELATED)){
		// Attempt to get transaction call

		if(failed_transaction != NULL){
			if(time(NULL) > last_transaction_timestamp + transaction_message_retry_interval * failed_transaction_count){
				call = failed_transaction;
				failed_transaction = NULL;
				last_transaction_timestamp = time(NULL);

				call_aquired = pdTRUE;
			}
		}

		if(call_aquired != pdTRUE && offline_enabled && non_enqueued_timestamp() < peek_txn_enqueue_timestamp()){

			char action[32] = "\0";
			void * cb_data = NULL;
			cJSON * message = non_enqueued_message(&cb_data);
			if(message == NULL || !cJSON_IsArray(message) || cJSON_GetArraySize(message) != 4){
				ESP_LOGE(TAG, "Expected non enqueued messsage but no message was %s", (message == NULL) ? "NULL" : "malformed");
			}
			else{
				strcpy(action, cJSON_GetArrayItem(message, 2)->valuestring);

				call = malloc(sizeof(struct ocpp_call_with_cb));
				if(call == NULL){
					ESP_LOGE(TAG, "Unable to allocate call for non enqueued transaction");
					xSemaphoreGive(ocpp_active_call_lock_1);
					return -1;
				}

				call->cb_data = cb_data;
				if(strcmp(action, OCPPJ_ACTION_START_TRANSACTION) == 0){
					call->call_message = message;
					call->result_cb = start_result_cb;
					call->error_cb = start_error_cb;

				}else if(strcmp(action, OCPPJ_ACTION_STOP_TRANSACTION) == 0){
					call->call_message = message;
					call->result_cb = stop_result_cb;
					call->error_cb = stop_error_cb;

				}else if(strcmp(action, OCPPJ_ACTION_METER_VALUES) == 0){
					call->call_message = message;
					call->result_cb = meter_result_cb;
					call->error_cb = meter_error_cb;

				}else{
					ESP_LOGE(TAG, "Non enqueued transaction message is not a know transaction related message. No callback will be used");
					call->call_message = message;
					call->result_cb = NULL;
					call->error_cb = NULL;
					call->cb_data = NULL;
				}
				call_aquired = pdTRUE;
				last_transaction_timestamp = time(NULL);
			}

		}

		if(call_aquired != pdTRUE){
			call_aquired = xQueueReceive(ocpp_transaction_call_queue, &call, pdMS_TO_TICKS(1));

			if(call_aquired == pdTRUE){
				get_txn_enqueue_timestamp();
				last_transaction_timestamp = time(NULL);
			}
		}

		if(call_aquired == pdTRUE){
			ESP_LOGI(TAG, "Sending next transaction related message");
			if(call->call_message != NULL && cJSON_IsArray(call->call_message) && cJSON_GetArraySize(call->call_message) >= 4){
				strncpy(last_transaction_id, cJSON_GetArrayItem(call->call_message, 1)->valuestring, 36);
				cJSON * payload = cJSON_GetArrayItem(call->call_message, 3);

				if(cJSON_HasObjectItem(payload, "transactionId")){
					cJSON * transaction_id_json = cJSON_GetObjectItem(payload, "transactionId");
					int new_id;
					if(convert_tmp_id(transaction_id_json->valueint, &new_id)){
						ESP_LOGW(TAG, "Replacing tmp id '%d' with '%d'", transaction_id_json->valueint, new_id);
						cJSON_SetNumberValue(transaction_id_json, new_id);
					}
				}
			}
		}
	}

	if(call_aquired != pdTRUE && !(call_blocking_mask & eOCPP_CALL_GENERIC)){ // Attempt to get generic call
		call_aquired = xQueueReceive(ocpp_call_queue, &call, pdMS_TO_TICKS(1));
	}

	if(call_aquired != pdTRUE){
		xSemaphoreGive(ocpp_active_call_lock_1);
		return -1;
	}

	active_call_id = cJSON_GetArrayItem(call->call_message, 1)->valuestring;
	active_call = call;

	char * action = cJSON_GetArrayItem(call->call_message, 2)->valuestring;

	int err = check_send_legality(action);

	if(err != 0){
		ESP_LOGE(TAG, "Could not send '%s' now, specification prohibits it", action);
		goto error;
	}

	char * message_string = cJSON_Print(call->call_message);
	if(message_string == NULL){
		ESP_LOGE(TAG, "Unable to create message string from call");
		goto error;
	}

	ESP_LOGI(TAG, "Sending next call (%s) [%s]", action, active_call_id);

	err = esp_websocket_client_send_text(client, message_string, strlen(message_string), pdMS_TO_TICKS(WEBSOCKET_WRITE_TIMEOUT));
	free(message_string);

	if(err == -1){
		ESP_LOGE(TAG, "Got websocket error when sending ocpp message");
		goto error;
	}
	last_call_timestamp = time(NULL);
	reset_heartbeat_timer();

	give_active_call();

	return last_listener_state;
error:
	fail_active_call("Not sendt");
	return -1;
}


void heartbeat_error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * data){
	if(error_code == NULL || error_description == NULL){
		ESP_LOGE(TAG, "Error response on heartbeat");
		return;
	}

	char * details = NULL;
	if(error_details != NULL){
		details = cJSON_Print(error_details);
	}

	if(details == NULL){
		ESP_LOGE(TAG, "Error response on heartbeat: [%s] %s", error_code, error_description);
		return;
	}

	ESP_LOGE(TAG, "Error response on heartbeat: [%s] %s, %s", error_code, error_description, details);
	free(details);
}

void heartbeat_result_cb(const char * unique_id, cJSON * payload, void * data){
	ESP_LOGI(TAG, "Response on heartbeat");

	time_t charge_point_time = time(NULL);

	if(cJSON_HasObjectItem(payload, "currentTime")){
		/*struct tm central_system_time_tm = parse_time(cJSON_GetObjectItem(payload, "currentTime")->valuestring);
		time_t central_system_time = mktime(&central_system_time_tm);*/
		time_t central_system_time = ocpp_parse_date_time(cJSON_GetObjectItem(payload, "currentTime")->valuestring);
		update_central_system_time_offset(charge_point_time, central_system_time);
	}else{
		ESP_LOGW(TAG, "Response to heartbeat did not contain currentTime");
	}
}

void update_transaction_message_related_config(uint8_t ocpp_transaction_message_attempts, uint16_t ocpp_transaction_message_retry_interval){
	transaction_message_attempts = ocpp_transaction_message_attempts;
	transaction_message_retry_interval = ocpp_transaction_message_retry_interval;
}

void update_heartbeat_timer(uint sec){
	if(heartbeat_handle != NULL){
		xTimerChangePeriod(heartbeat_handle, pdMS_TO_TICKS(sec * 1000), pdMS_TO_TICKS(100));
	}
}

void ocpp_heartbeat(){
	cJSON * heartbeat_request = ocpp_create_heartbeat_request();
	if(heartbeat_request == NULL){
		ESP_LOGE(TAG, "Unable to create heartbeat");
		return;
	}

	if(enqueue_call(heartbeat_request, heartbeat_result_cb, heartbeat_error_cb, NULL, eOCPP_CALL_GENERIC) != 0){
		ESP_LOGE(TAG, "Unable to send heartbeat");
		cJSON_Delete(heartbeat_request);
	}
}

int start_ocpp_heartbeat(void){
	long actual_interval = heartbeat_interval;

	if(heartbeat_interval < MINIMUM_HEARTBEAT_INTERVAL || heartbeat_interval > MAX_HEARTBEAT_INTERVAL){
		ESP_LOGE(TAG, "Unable to start heartbeat with interval %ld", heartbeat_interval);

		if(heartbeat_interval < MINIMUM_HEARTBEAT_INTERVAL){
			actual_interval = MINIMUM_HEARTBEAT_INTERVAL;

		}else if(heartbeat_interval > MAX_HEARTBEAT_INTERVAL){
			actual_interval = MAX_HEARTBEAT_INTERVAL;
		}
		ESP_LOGE(TAG, "Using an interval of %ld instead", actual_interval);
	}

	ESP_LOGI(TAG, "starting heartbeat with interval %ld sec", actual_interval);

	heartbeat_handle = xTimerCreate("Ocpp Heartbeat", pdMS_TO_TICKS(actual_interval * 1000),
					pdTRUE, NULL, ocpp_heartbeat);

	if(heartbeat_handle == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for heartbeat timer");
		return -1;
	}

	if(xTimerStart(heartbeat_handle, pdMS_TO_TICKS(500)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to schedule heartbeat timer");
		return -1;
	}

	return 0;
}

void stop_ocpp_heartbeat(void){

	if(heartbeat_handle != NULL && xTimerDelete(heartbeat_handle, pdMS_TO_TICKS(500)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to stop heartbeat timer");
	}
	heartbeat_handle = NULL;
	heartbeat_interval = -1;
}

int start_ocpp(const char * url, const char * charger_id, uint32_t ocpp_heartbeat_interval, uint8_t ocpp_transaction_message_attempts, uint16_t ocpp_transaction_message_retry_interval){
	ESP_LOGI(TAG, "Starting ocpp");
	default_heartbeat_interval = ocpp_heartbeat_interval;

	transaction_message_attempts = ocpp_transaction_message_attempts;
	transaction_message_retry_interval = ocpp_transaction_message_retry_interval;

	char uri[256];
	int written_length = snprintf(uri, sizeof(uri), "%s/%s", url, charger_id);

	if(written_length < 0 || written_length >= sizeof(uri)){
		ESP_LOGE(TAG, "Unable to write uri to buffer");
		return -1;
	}
	ESP_LOGI(TAG, "Websocket url used is: %s", uri);
	esp_websocket_client_config_t websocket_cfg = {
		.uri = uri,
		.subprotocol = "ocpp1.6",
		.task_stack = 4096,
		.buffer_size = WEBSOCKET_BUFFER_SIZE,
	};

	client = esp_websocket_client_init(&websocket_cfg);
	if(client == NULL){
		ESP_LOGE(TAG, "Unable to create websocket client");
		return -1;
	}

	esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to register events");
		goto error;
	}

	ESP_LOGI(TAG, "Starting websocket client");
	err = esp_websocket_client_start(client);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to start websocket client");
		goto error;
	}

	for(int i = 0; i < 5; i++){
		if(websocket_connected){
			ocpp_call_queue = xQueueCreate(5, sizeof(struct ocpp_call_with_cb *));
			ocpp_blocking_call_queue = xQueueCreate(1, sizeof(struct ocpp_call_with_cb *));
			ocpp_transaction_call_queue = xQueueCreate(MAX_TRANSACTION_QUEUE_SIZE, sizeof(struct ocpp_call_with_cb *));

			if(ocpp_call_queue == NULL || ocpp_transaction_call_queue == NULL || ocpp_blocking_call_queue == NULL){
				ESP_LOGE(TAG, "Unable to create call queues");
				goto error;
			}

			ocpp_active_call_lock_1 = xSemaphoreCreateBinary();
			if(ocpp_active_call_lock_1 == NULL){
				ESP_LOGE(TAG, "Unable to create semaphore");
				goto error;
			}

			if(xSemaphoreGive(ocpp_active_call_lock_1) != pdTRUE){
				ESP_LOGE(TAG, "Unable to open semaphore");
				goto error;
			}
			return 0;
		}

		if(i+1 == 5){
			ESP_LOGE(TAG, "Websocket did not connect");
			goto error;
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

error:
	stop_ocpp();
	return -1;
}

void boot_result_cb(const char * unique_id, cJSON * payload, void * data){
	ESP_LOGI(TAG, "Recieved boot notification result");
	time_t charge_point_time = time(NULL);

	if(cJSON_HasObjectItem(payload, "currentTime")){
		time_t central_system_time = ocpp_parse_date_time(cJSON_GetObjectItem(payload, "currentTime")->valuestring);
		update_central_system_time_offset(charge_point_time, central_system_time);
	}else{
		ESP_LOGE(TAG, "Boot result is lacking currentTime");
	}

	if(cJSON_HasObjectItem(payload, "interval")){
		heartbeat_interval = cJSON_GetObjectItem(payload, "interval")->valueint;
		ESP_LOGI(TAG, "Heartbeat interval is: %ld", heartbeat_interval);
	}else{
		ESP_LOGE(TAG, "Boot result is lacking interval");
	}

	if(cJSON_HasObjectItem(payload, "status")){
		char * status_string = cJSON_GetObjectItem(payload, "status")->valuestring;

		if(strcmp(status_string, OCPP_REGISTRATION_ACCEPTED) == 0){
			ESP_LOGI(TAG, "Boot accepted");
			registration_status = eOCPP_REGISTRATION_ACCEPTED;
		}
		else if(strcmp(status_string, OCPP_REGISTRATION_PENDING) == 0){
			ESP_LOGW(TAG, "Boot still pending");
			registration_status = eOCPP_REGISTRATION_PENDING;
		}
		else if(strcmp(status_string, OCPP_REGISTRATION_REJECTED) == 0){
			ESP_LOGE(TAG, "Boot rejected");
			registration_status = eOCPP_REGISTRATION_REJECTED;
		}
		else{
			ESP_LOGE(TAG, "Got unrecognised registration status '%s'", status_string);
			ESP_LOGE(TAG, "Assuming rejected");
			registration_status = eOCPP_REGISTRATION_REJECTED;
		}

	}else{
		ESP_LOGE(TAG, "Boot result without error is lacking status, assuming REJECTED");
	}

	// The specification indicates that interval can be 0 and that its meaning depends on the given status:
	// "If that interval value is zero, the Charge Point chooses a waiting interval on its
	// own, in a way that avoids flooding the Central System with requests"
	//
	// "If the Central System returns something other than Accepted, the value of the interval field
	// indicates the minimum wait time before sending a next BootNotification request"
    	if(registration_status == eOCPP_REGISTRATION_ACCEPTED && heartbeat_interval == 0){
		heartbeat_interval = default_heartbeat_interval;

	}else if(registration_status != eOCPP_REGISTRATION_ACCEPTED && heartbeat_interval == 0){
		// No recommendations are given for delay to next BootNotification request
		heartbeat_interval = 10; // We use relatively short delay as the websocket is active anyway
	}
}

void boot_error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * data){
	if(error_code == NULL || error_description == NULL){
		ESP_LOGE(TAG, "Error response on boot notification");
	}
	else{
		ESP_LOGE(TAG, "Error response from boot notification: [%s] %s", error_code, error_description);
	}
	registration_status = eOCPP_REGISTRATION_REJECTED;
}

char boot_parameter_chargebox_nr[26] = {0};
char boot_parameter_charge_point_model[21] = {0};
char boot_parameter_cp_nr[26] = {0};
char boot_parameter_cp_vendor[21] = {0};
char boot_parameter_firm_ver[51] = {0};
char boot_parameter_iccid[21] = {0};
char boot_parameter_imsi[21] = {0};
char boot_parameter_meter_nr[26] = {0};
char boot_parameter_meter_type[26] = {0};

int enqueue_boot_notification(){
	if(boot_parameter_charge_point_model[0] == '\0' || boot_parameter_cp_vendor[0] == '\0'){
		ESP_LOGE(TAG, "Boot notification can not be created due to missing model and/or vendor parameter");
		return -1;
	}

	ESP_LOGI(TAG,"Creating boot notification with parameters:\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n%-23s: %s\n",
		"Chargebox serial nr", (boot_parameter_chargebox_nr[0] != '\0') ? boot_parameter_chargebox_nr : "(Omitted)",
		"Charge point model", boot_parameter_charge_point_model,
		"Charge point serial nr", (boot_parameter_cp_nr[0] != '\0') ? boot_parameter_cp_nr : "(Omitted)",
		"Charge point vendor", boot_parameter_cp_vendor,
		"Firmware version", (boot_parameter_firm_ver[0] != '\0') ? boot_parameter_firm_ver : "(Omitted)",
		"ICCID", (boot_parameter_iccid[0] != '\0') ? boot_parameter_iccid : "(Omitted)",
		"IMSI", (boot_parameter_imsi[0] != '\0') ? boot_parameter_imsi : "(Omitted)",
		"eMeter serial nr", (boot_parameter_meter_nr[0] != '\0') ? boot_parameter_meter_nr : "(Omitted)",
		"eMeter type", (boot_parameter_meter_type[0] != '\0') ? boot_parameter_meter_type : "(Omitted)");

	cJSON * boot_notification = ocpp_create_boot_notification_request((boot_parameter_chargebox_nr[0] != '\0') ? boot_parameter_chargebox_nr : NULL,
									boot_parameter_charge_point_model,
									(boot_parameter_cp_nr[0] != '\0') ? boot_parameter_cp_nr : NULL,
									boot_parameter_cp_vendor,
									(boot_parameter_firm_ver[0] != '\0') ? boot_parameter_firm_ver : NULL,
									(boot_parameter_iccid[0] != '\0') ? boot_parameter_iccid : NULL,
									(boot_parameter_imsi[0] != '\0') ? boot_parameter_imsi : NULL,
									(boot_parameter_meter_nr[0] != '\0') ? boot_parameter_meter_nr : NULL,
									(boot_parameter_meter_type[0] != '\0') ? boot_parameter_meter_type : NULL);

	if(boot_notification == NULL){
		ESP_LOGE(TAG, "Unable to create boot notification");
		return -1;
	}

	if(enqueue_call(boot_notification, boot_result_cb, boot_error_cb, NULL, eOCPP_CALL_BLOCKING) != 0){
		ESP_LOGE(TAG, "Unable to equeue BootNotification");
		cJSON_Delete(boot_notification);
		return -1;
	}

	return 0;
}

int complete_boot_notification_process(const char * chargebox_serial_number, const char * charge_point_model,
				const char * charge_point_serial_number, const char * charge_point_vendor,
				const char * firmware_version, const char * iccid, const char * imsi,
				const char * meter_serial_number, const char * meter_type){

	if(chargebox_serial_number != NULL && chargebox_serial_number[0] != '\0'
		&& is_ci_string_type(chargebox_serial_number, 25)){

		strcpy(boot_parameter_chargebox_nr, chargebox_serial_number);
	}

	strcpy(boot_parameter_charge_point_model, charge_point_model);

	if(charge_point_serial_number != NULL && charge_point_serial_number[0] != '\0'
		&& is_ci_string_type(charge_point_serial_number, 25)){

		strcpy(boot_parameter_cp_nr, charge_point_serial_number);
	}

	strcpy(boot_parameter_cp_vendor, charge_point_vendor);

	if(firmware_version != NULL && firmware_version[0] != '\0'
		&& is_ci_string_type(firmware_version, 50)){

		strcpy(boot_parameter_firm_ver, firmware_version);
	}

	if(iccid != NULL && iccid[0] != '\0'
		&& is_ci_string_type(iccid, 20)){

		strcpy(boot_parameter_iccid, iccid);
	}

        if(imsi != NULL && imsi[0] != '\0'
		&& is_ci_string_type(imsi, 20)){

		strcpy(boot_parameter_imsi, imsi);
	}

	if(meter_serial_number != NULL && meter_serial_number[0] != '\0'
		&& is_ci_string_type(meter_serial_number, 25)){

		strcpy(boot_parameter_meter_nr, meter_serial_number);
	}

	if(meter_type != NULL && meter_type[0] != '\0'
		&& is_ci_string_type(meter_type, 25)){

		strcpy(boot_parameter_meter_type, meter_type);
	}

	registration_status = eOCPP_REGISTRATION_PENDING;
	heartbeat_interval = -1;

	if(enqueue_boot_notification() != 0){
		ESP_LOGE(TAG, "Failed boot notification");
		return -1;
	}

	ESP_LOGI(TAG, "Sending boot notification message");
	if(send_next_call(eOCPP_WEBSOCKET_NO_EVENT) != 0){
		ESP_LOGE(TAG, "Unable to send message");
		return -1;
	}

	ESP_LOGI(TAG, "Waiting for response to complete...");
	bool is_retry = false;
	for(int i = 0; i < 5; i++){
		if(registration_status != eOCPP_REGISTRATION_PENDING || heartbeat_interval != -1){

			if(registration_status == eOCPP_REGISTRATION_ACCEPTED){
				return 0;
			}else if(!is_retry){
				// If boot is not accepted, a new boot notification should be sendt after given interval.
				vTaskDelay(pdMS_TO_TICKS(heartbeat_interval * 1000));
				is_retry = true;

				if(enqueue_boot_notification() != 0){
					ESP_LOGE(TAG, "Failed new boot notification");
					return -1;

				}

				registration_status = eOCPP_REGISTRATION_PENDING;
				heartbeat_interval = -1;

				ESP_LOGI(TAG, "Sending new boot notification message");
				if(send_next_call(eOCPP_WEBSOCKET_NO_EVENT) != 0){
					ESP_LOGE(TAG, "Unable to send new boot notification message");
					return -1;
				}
				i = 0;
			}
			return (registration_status == eOCPP_REGISTRATION_ACCEPTED) ? 0 : -1;
		}
		ESP_LOGW(TAG, "WAITING...");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	ESP_LOGE(TAG, "Boot status not updated");
	return -1;
}

void stop_ocpp(void){
	ESP_LOGW(TAG, "Closing web socket");
	/* if(esp_websocket_client_is_connected(client)){ */
	/* 	//Only awailable in newer versions of esp-idf */
	/* 	esp_websocket_client_close(client, pdMS_TO_TICKS(5000)); */
	/* } */
	if(client != NULL){
		esp_websocket_client_destroy(client); // Calls esp_websocket_client_stop internaly
		client = NULL;
	}

	if(ocpp_call_queue != NULL){
		vQueueDelete(ocpp_call_queue);
		ocpp_call_queue = NULL;
	}

	if(ocpp_blocking_call_queue != NULL){
		vQueueDelete(ocpp_blocking_call_queue);
		ocpp_blocking_call_queue = NULL;
	}

	if(ocpp_transaction_call_queue != NULL){
		vQueueDelete(ocpp_transaction_call_queue);
		ocpp_blocking_call_queue = NULL;
	}

	if(ocpp_active_call_lock_1 != NULL){
		vSemaphoreDelete(ocpp_active_call_lock_1);
		ocpp_active_call_lock_1 = NULL;
	}

	clean_listener();

	clear_active_call();
	block_enqueue_call(0);
	block_sending_call(0);

	ESP_LOGW(TAG, "Web socket closed and state cleared");
	return;
}

int handle_ocpp_call(int last_listener_state){
	return send_next_call(last_listener_state);
}
