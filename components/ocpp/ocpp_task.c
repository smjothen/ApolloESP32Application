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
#include "ocpp_json/ocppj_message_structure.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_date_time.h"
#include "types/ocpp_charge_point_error_code.h"

static const char *TAG = "OCPP_TASK";

// TODO: Tune, currently above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
// to prevent use of DMA. esp_websocket_client allocates this twice (for rx_buffer and tx_buffer)
#define WEBSOCKET_BUFFER_SIZE 2048

esp_websocket_client_handle_t client = NULL;

SemaphoreHandle_t ocpp_active_call_lock_1 = NULL;

/**
 * The specification allows only one active call from the charge point:
 * "A Charge Point or Central System SHOULD NOT send a CALL message to the other party unless all the
 * CALL messages it sent before have been responded to or have timed out."
 *
 * We still use a queue for the active call with a single item to prevent ocpp_task from setting a new
 * message as active until it has been taken by ocpp_listener. We also use the freeRTOS peek functionality
 * of queue to check if received response corresponds to active call.
 */
QueueHandle_t ocpp_active_call_queue = NULL; // queue size 1

bool is_trigger_message = false; // Will always be false until TriggerMessage is implemented

time_t last_call_timestamp = {0};

QueueHandle_t ocpp_call_queue = NULL; // For normal messages
QueueHandle_t ocpp_blocking_call_queue = NULL; // For messages that prevent significant ocpp behaviour (BootNotification)
QueueHandle_t ocpp_transaction_call_queue = NULL; // transactions SHOULD be delivered as soon as possible, in chronological order, MUST queue when offline

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

static uint16_t ocpp_call_timeout = 10;
static uint32_t ocpp_minimum_status_duration = 3;

TimerHandle_t message_timeout_handle = NULL;

static uint8_t transaction_message_attempts = 3;
static uint8_t transaction_message_retry_interval = 60;

struct ocpp_active_call * failed_transaction_call = NULL; // Transaction message that should be attempted again
struct ocpp_active_call * awaiting_failed = NULL; // Transaction message that was attempted to be sent, but blocked by failed transaction.

long int central_system_time_offset = 0;
time_t (*non_enqueued_timestamp)();
cJSON * (*non_enqueued_message)(void ** cb_data);

bool offline_enabled = false;

TaskHandle_t task_to_notify;
uint task_notify_offset = 0;

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

	if(oldest_non_enqueued_timestamp() != LONG_MAX && task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);
}

void ocpp_notify_offline_enqueued(){
	if(task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);
}

void ocpp_change_message_timeout(uint16_t timeout){
	ocpp_call_timeout = timeout;
	if(message_timeout_handle != NULL){
		xTimerChangePeriod(message_timeout_handle, pdMS_TO_TICKS(ocpp_call_timeout * 1000), pdMS_TO_TICKS(1000));
	}
}

void ocpp_change_minimum_status_duration(uint32_t duration){
	ocpp_minimum_status_duration = duration;
}


enum ocpp_registration_status get_registration_status(){
	return registration_status;
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

int add_call(struct ocpp_call_with_cb * message, enum call_type type, TickType_t wait){
	if(ocpp_call_queue == NULL)
		return -1;

	switch(type){
	case eOCPP_CALL_GENERIC:
		if(xQueueSendToBack(ocpp_call_queue, &message, wait) != pdPASS){
			return -1;
		}
		break;
	case eOCPP_CALL_TRANSACTION_RELATED:
		if(xQueueSendToBack(ocpp_transaction_call_queue, &message, wait) != pdPASS){
			return -1;
		}else{
			set_txn_enqueue_timestamp(time(NULL));
		}
		break;
	case eOCPP_CALL_BLOCKING:
		if(xQueueSendToBack(ocpp_blocking_call_queue, &message, wait) != pdPASS){
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

int enqueue_call_generic(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type, TickType_t wait){
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

	int err = add_call(message_with_cb, type, wait);

	if(err != 0){
		ESP_LOGE(TAG, "Unable to enqueue call");
		free(message_with_cb);
	}

	if(task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

	return err;
}

int enqueue_call(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type){
	return enqueue_call_generic(call, result_cb, error_cb, cb_data, type, pdMS_TO_TICKS(500));
}

int enqueue_call_immediate(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type){
	return enqueue_call_generic(call, result_cb, error_cb, cb_data, type, 0);
}

static uint8_t call_blocking_mask = 0;

void block_sending_call(uint8_t call_type_mask){
	call_blocking_mask = call_type_mask;
}

void ocpp_configure_task_notification(TaskHandle_t task, uint offset){
	task_to_notify = task;
	task_notify_offset = offset;
}

//TODO: consider adding offlineSession calls or removing
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

void message_timeout(){
	if(uxQueueMessagesWaiting(ocpp_active_call_queue) > 0)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_TIMEOUT << task_notify_offset, eSetBits);
}

static void reset_message_timeout(){
	if(message_timeout_handle != NULL){
		xTimerReset(message_timeout_handle, pdMS_TO_TICKS(100));
	}else{
		message_timeout_handle = xTimerCreate("Ocpp message timeout",
						pdMS_TO_TICKS(ocpp_call_timeout * 1000),
						pdFALSE, NULL, message_timeout);

		if(message_timeout_handle == NULL){
			ESP_LOGE(TAG, "Unable to create message timeout timer");

		}else if(xTimerStart(message_timeout_handle, 1000) != pdPASS){
			ESP_LOGE(TAG, "Unable to start created message timout timer");
		}
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

	cJSON_Delete(call);

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

SemaphoreHandle_t status_notification_lock = NULL;
cJSON * awaiting_status_notification = NULL;

static void send_status_notification(cJSON * status_notification){
	ESP_LOGI(TAG, "Sending status notification to queue");
	if(enqueue_call(status_notification, NULL, NULL, "status notification", eOCPP_CALL_GENERIC) != 0){
		ESP_LOGE(TAG, "Unable to enqueue status notification");
		cJSON_Delete(status_notification);
	}
}

TimerHandle_t status_notification_handle = NULL;

static void send_and_clear_notification(){
	if(status_notification_lock == NULL || xSemaphoreTake(status_notification_lock, 0) != pdTRUE){
		ESP_LOGW(TAG, "Unable to aquire lock to send notification. Preparation of next notification should be in progress");
		return;
	}

	if(awaiting_status_notification != NULL){
		send_status_notification(awaiting_status_notification);
		awaiting_status_notification = NULL;
	}else{
		ESP_LOGW(TAG, "Awaited status notification is NULL");
	}

	xSemaphoreGive(status_notification_lock);
}

static int replace_awaiting_status_notification(cJSON * status_notification){
	if(status_notification_lock == NULL || xSemaphoreTake(status_notification_lock, pdMS_TO_TICKS(500)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to aquire lock to replace awaiting status notification");
		return -1;
	}

	if(status_notification_handle == NULL){
		status_notification_handle = xTimerCreate("Ocpp status notification",
							pdMS_TO_TICKS(ocpp_minimum_status_duration * 1000),
							pdFALSE, NULL, send_and_clear_notification);

		if(status_notification_handle == NULL){
			ESP_LOGE(TAG, "Unable to create notification handle");
			goto error;
		}
	}

	if(xTimerStop(status_notification_handle, pdMS_TO_TICKS(500)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to stop current notification");
		goto error;
	}

	cJSON_Delete(awaiting_status_notification);
	awaiting_status_notification = status_notification;

	if(xTimerGetPeriod(status_notification_handle) != pdMS_TO_TICKS(ocpp_minimum_status_duration * 1000))
		xTimerChangePeriod(status_notification_handle, pdMS_TO_TICKS(ocpp_minimum_status_duration * 1000), pdMS_TO_TICKS(500));

	xTimerReset(status_notification_handle, pdMS_TO_TICKS(500));

	xSemaphoreGive(status_notification_lock);
	return 0;

error:
	xSemaphoreGive(status_notification_lock);
	return -1;
}

static void stop_awaiting_status_notification(){
	if(status_notification_lock != NULL && xSemaphoreTake(status_notification_lock, 0) == pdTRUE){
		if(status_notification_handle != NULL && xTimerStop(status_notification_handle, 500) == pdTRUE){
			cJSON_Delete(awaiting_status_notification);
			awaiting_status_notification = NULL;
		}

		xSemaphoreGive(status_notification_lock);
	}
}

enum ocpp_cp_status_id last_known_state = -1;

void ocpp_send_status_notification(enum ocpp_cp_status_id new_state, const char * error_code, const char * info, bool important){
	ESP_LOGI(TAG, "Preparing status notification");

	if(new_state == -1){
		ESP_LOGW(TAG, "Status notification has no known state using last state");
		new_state = last_known_state;
	}

	const char * state = ocpp_cp_status_from_id(new_state);

	if(new_state == last_known_state && strcmp(error_code, OCPP_CP_ERROR_NO_ERROR) == 0 && info == NULL && important == false){
		ESP_LOGW(TAG, "Ignoring status notification for '%s': No new information", state);
		return;
	}

	last_known_state = new_state;

	if(state == NULL){
		ESP_LOGE(TAG, "Unknown status id: %d", new_state);
		return;
	}

	cJSON * status_notification  = ocpp_create_status_notification_request(1, error_code, info, state, time(NULL), NULL, NULL);
	if(status_notification == NULL){
		ESP_LOGE(TAG, "Unable to create status notification request");
		return;
	}

	if(!important && ocpp_minimum_status_duration > 0){
		if(replace_awaiting_status_notification(status_notification) != 0){
			ESP_LOGE(TAG, "Unable to replace awaiting status notification.");
			cJSON_Delete(status_notification);
		}

	}else{
		stop_awaiting_status_notification();
		send_status_notification(status_notification);
	}
}


bool check_active_call_validity(struct ocpp_active_call * call){
	return call != NULL && check_call_with_cb_validity(call->call);
}

int check_send_legality(const char * action){
	if(action == NULL)
		return -1;

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

void update_transaction_id(struct ocpp_active_call * call){
	if(!check_active_call_validity(call)){
		ESP_LOGE(TAG, "Unable to update tmp id: Invalid active call");
		return;
	}

	cJSON * message_payload = cJSON_GetArrayItem(call->call->call_message, OCPPJ_CALL_INDEX_PAYLOAD);
	if(message_payload == NULL){
		ESP_LOGE(TAG, "Unable to update tmp id: No payload");
		return;
	}
	cJSON * transaction_id_json = cJSON_GetObjectItem(message_payload, "transactionId");
	if(transaction_id_json == NULL){
		ESP_LOGE(TAG, "Unable to update tmp id: No transactionId");
		return;
	}
	int new_id;
	if(convert_tmp_id(transaction_id_json->valueint, &new_id)){
		ESP_LOGW(TAG, "Replacing tmp id '%d' with '%d'", transaction_id_json->valueint, new_id);
		cJSON_SetNumberValue(transaction_id_json, new_id);
	}
}

BaseType_t take_active_call_if_match(struct ocpp_active_call * call, const char * unique_id, uint timeout_ms){

	xSemaphoreTake(ocpp_active_call_lock_1, portMAX_DELAY);

	BaseType_t ret = xQueuePeek(ocpp_active_call_queue, call, timeout_ms);
	if(ret == pdTRUE){
		if(call->call != NULL){
			const char * active_id = ocppj_get_unique_id_from_call(call->call->call_message);
			if(active_id == NULL || strcmp(active_id, unique_id) != 0){
				ret = pdFALSE;
			}else{
				//Call matches, remove it from the queue
				xQueueReset(ocpp_active_call_queue); // Use reset as it cannot fail and recieve would not give more info
			}
		}else{
			ret = pdFALSE;
		}
	}

	xSemaphoreGive(ocpp_active_call_lock_1);

	return ret;
}

void timeout_active_call(){
	xSemaphoreTake(ocpp_active_call_lock_1, portMAX_DELAY);

	ESP_LOGE(TAG, "Unable to send to ocpp_active_call_queue: active timed out");
	struct ocpp_active_call timed_out_call;
	if(xQueueReceive(ocpp_active_call_queue, &timed_out_call, 0) == pdTRUE){
		fail_active_call(&timed_out_call, OCPPJ_ERROR_INTERNAL, "CP timeout", NULL);
	}else{
		ESP_LOGE(TAG, "Unable to take timed out call, Did listener just receive it?");
	}

	// Remove the timed out call if we were not able to take it, ensuring that we should be able to send the next call
	xQueueReset(ocpp_active_call_queue); // At the time of writing; xQueueReset may only return pdTRUE

	xSemaphoreGive(ocpp_active_call_lock_1);
}
// Used if the active call was sendt or attempted to be sendt to allow retrying transaction related messages. Returns true if needs retries.
void fail_active_call(struct ocpp_active_call * call, const char * error_code, const char * error_description, cJSON * error_details){
	if(call != NULL && call->call != NULL){
		if(call->is_transaction_related){
			ESP_LOGE(TAG, "Failing transaction related call");
			call->retries++;

			if(call->retries < transaction_message_attempts){
				ESP_LOGW(TAG, "Preparing for retransmit after error code '%s'", error_code  != NULL ? error_code : "UNKNOWN");

				failed_transaction_call = calloc(sizeof(struct ocpp_active_call), 1);
				if(failed_transaction_call == NULL){
					ESP_LOGE(TAG, "Failed to allocate memory for failed transaction related call. Will call error cb");
				}else{

					memcpy(failed_transaction_call, call, sizeof(struct ocpp_active_call));
					return;
				}
			}else{
				ESP_LOGE(TAG, "Transaction retry attempts exceeded");
			}
		}else{
			ESP_LOGE(TAG, "Failing call");
		}

		const char * unique_id = ocppj_get_unique_id_from_call(call->call->call_message);

		// If call is transaction related, then only the last error will be given to the callback
		if(call->call->error_cb != NULL){
			call->call->error_cb(unique_id, error_code, error_description, error_details, call->call->cb_data);
		}else{
			error_logger(unique_id, error_code, error_description, error_details, call->call->cb_data);
		}
		free_call_with_cb(call->call);
		call->call = NULL;
	}
}

int get_waiting_transaction_messages(){
	int ret = 0;

	if(failed_transaction_call != NULL)
		ret++;
	if(awaiting_failed != NULL)
		ret++;

	if(non_enqueued_timestamp() != LONG_MAX)
		ret+=2; // We dont know how many are on file, but return 2 to ensure that new check will occure

	ret += uxQueueMessagesWaiting(ocpp_transaction_call_queue);
	return ret;
}

BaseType_t receive_transaction_call(struct ocpp_active_call * call, TickType_t wait, uint * min_wait_out){

	if(failed_transaction_call != NULL){
		uint fail_wait = (transaction_message_retry_interval * failed_transaction_call->retries * (failed_transaction_call->retries+1)) / 2;

		if(time(NULL) > fail_wait + failed_transaction_call->timestamp){
			memcpy(call, failed_transaction_call, sizeof(struct ocpp_active_call));
			free(awaiting_failed);
			failed_transaction_call = NULL;

			return pdTRUE;
		}else{
			*min_wait_out = fail_wait - (time(NULL) - failed_transaction_call->timestamp);
			return pdFALSE;
		}
	}

	*min_wait_out = 0; // Not waiting for failed transaction related message retry

	if(awaiting_failed != NULL){
		memcpy(call, awaiting_failed, sizeof(struct ocpp_active_call));
		free(awaiting_failed);
		awaiting_failed = NULL;

		return pdTRUE;
	}

	if(offline_enabled){
		struct ocpp_call_with_cb * call_with_cb = NULL;

		while(non_enqueued_timestamp() < peek_txn_enqueue_timestamp()){
			if(call_with_cb == NULL){
				call_with_cb = malloc(sizeof(struct ocpp_call_with_cb));
				if(call_with_cb == NULL){
					ESP_LOGE(TAG, "Unable to allocate call for non enqueued transaction");
					return pdFALSE;
				}
			}

			call_with_cb->call_message = non_enqueued_message(&call_with_cb->cb_data);

			if(check_call_with_cb_validity(call_with_cb) == false){
				ESP_LOGE(TAG, "Invalid message on file");
			}else{
				const char * action = ocppj_get_action_from_call(call_with_cb->call_message);
				if(strcmp(action, OCPPJ_ACTION_START_TRANSACTION) == 0){
					call_with_cb->result_cb = start_result_cb;
					call_with_cb->error_cb = start_error_cb;

				}else if(strcmp(action, OCPPJ_ACTION_STOP_TRANSACTION) == 0){
					call_with_cb->result_cb = stop_result_cb;
					call_with_cb->error_cb = stop_error_cb;

				}else if(strcmp(action, OCPPJ_ACTION_METER_VALUES) == 0){
					call_with_cb->result_cb = meter_result_cb;
					call_with_cb->error_cb = meter_error_cb;

				}else{
					ESP_LOGE(TAG, "Non enqueued transaction message is not a know transaction related message. No callback will be used");
					call_with_cb->result_cb = NULL;
					call_with_cb->error_cb = NULL;
				}

				call->call = call_with_cb;
				call->is_transaction_related = true;

				return pdTRUE;
			}
		}
	}

	if(xQueueReceive(ocpp_transaction_call_queue, &call->call, wait) == pdTRUE){
		call->is_transaction_related = true;
		get_txn_enqueue_timestamp();
		return pdTRUE;
	}

	return pdFALSE;
}

int send_next_call(int * remaining_call_count_out){

	struct ocpp_active_call call = {0};

	UBaseType_t blocking_count = 0;
	UBaseType_t transaction_count = 0;
	UBaseType_t generic_count = 0;

	if(!(call_blocking_mask & eOCPP_CALL_BLOCKING))
		blocking_count = uxQueueMessagesWaiting(ocpp_blocking_call_queue);

	if(!(call_blocking_mask & eOCPP_CALL_TRANSACTION_RELATED))
		transaction_count = get_waiting_transaction_messages();

	if(!(call_blocking_mask & eOCPP_CALL_GENERIC))
		generic_count = uxQueueMessagesWaiting(ocpp_call_queue);

	*remaining_call_count_out = blocking_count + transaction_count + generic_count;

	BaseType_t call_aquired  = pdFALSE;
	if(blocking_count > 0)
		call_aquired = xQueueReceive(ocpp_blocking_call_queue, &call.call, pdMS_TO_TICKS(2500));

	uint min_wait;
	if(!call_aquired && transaction_count > 0){
		call_aquired = receive_transaction_call(&call, pdMS_TO_TICKS(2500), &min_wait);

		if(call_aquired){
			update_transaction_id(&call);
		}
	}

	if(!call_aquired && generic_count > 0)
		call_aquired = xQueueReceive(ocpp_call_queue, &call.call, pdMS_TO_TICKS(2500));

	if(!call_aquired){
		return 0;
	}else{
		(*remaining_call_count_out)--; // We removed call from queue or file

		if(call.timestamp == 0) // If the call does not have a previous timestamp, then it was created now
			call.timestamp = time(NULL);
	}

	// Wait for currently active call to be handled or timeout, and set next call as active
	BaseType_t activated = xQueueSendToBack(ocpp_active_call_queue, &call, pdMS_TO_TICKS(ocpp_call_timeout * 1000));

	if(activated == pdFALSE){
		timeout_active_call();
		activated = xQueueSendToBack(ocpp_active_call_queue, &call, 0);

		if(!activated){
			/*
			 * If we are still not able to set the new call as active after a queue reset.
			 * Then somthing is dreadfully wrong and we discard the call we are trying to send.
			 */
			ESP_LOGE(TAG, "Unable to set new call as active despite queue reset");
			return -1;
		}
	}

	/*
	 * We have set a new message as active, but if the last message failed and was a transaction. Then that message
	 * is chronologically before the new message. If the new message is also a transaction, then the failed message
	 * blocks the new message and we need to set the current message as awaiting completion of the failed message.
	 */
	if(call.is_transaction_related && failed_transaction_call != NULL){ // The message we intended to send is now blocked
		ESP_LOGW(TAG, "Transaction message selected for next active call is blocked by failed message");

		awaiting_failed = calloc(sizeof(struct ocpp_active_call), 1);
		if(awaiting_failed == NULL){
			ESP_LOGE(TAG, "Unable to create buffer for awaiting call. Message may be in incorrect order");
		}else{

			if(xQueueReceive(ocpp_active_call_queue, awaiting_failed, 0) != pdTRUE){
				ESP_LOGE(TAG, "Unable to remove active transaction message that should have been blocked. Message may be in incorrect order");

				free(awaiting_failed);
				awaiting_failed = NULL;
			}else{
				if(generic_count > 0){
					if(xQueueReceive(ocpp_call_queue, &call.call, pdMS_TO_TICKS(2500)) == pdTRUE){
						call.is_transaction_related = false;
						call.timestamp = time(NULL);

						if(xQueueSend(ocpp_active_call_queue, &call, pdMS_TO_TICKS(2500)) != pdTRUE){
							fail_active_call(&call, OCPPJ_ERROR_INTERNAL,"CP unable to set active call after transaction was blocked by failed transaction", NULL);
							return -1;
						}
					}else{
						ESP_LOGE(TAG, "Unable to aquire next non transaction call");
						return -1;
					}
				}else{
					ESP_LOGW(TAG, "No alternative call to send");
					return 0;
				}
			}
		}
	}

	if(check_active_call_validity(&call) != true){
		ESP_LOGE(TAG, "Active call is invalid");

		fail_active_call(&call, OCPPJ_ERROR_INTERNAL, "CP call invalid when set as active", NULL);
		xQueueReset(ocpp_active_call_queue);
		return -1;
	}

	const char * action = ocppj_get_action_from_call(call.call->call_message);

	int err = check_send_legality(action);

	if(err != 0){
		ESP_LOGE(TAG, "Could not send '%s' now, specification prohibits it", action);

		fail_active_call(&call, OCPPJ_ERROR_INTERNAL, "CP call prohibited by state", NULL);
		xQueueReset(ocpp_active_call_queue);
		return -1;
	}

	char * message_string = cJSON_Print(call.call->call_message);
	if(message_string == NULL){
		ESP_LOGE(TAG, "Unable to create message string from call");

		fail_active_call(&call, OCPPJ_ERROR_INTERNAL, "CP unable to serialize", NULL);
		xQueueReset(ocpp_active_call_queue);
		return -1;
	}

	const char * active_call_id = ocppj_get_unique_id_from_call(call.call->call_message);
	ESP_LOGI(TAG, "Sending next call (%s) [%s]", action, active_call_id != NULL ? active_call_id : "NULL");

	ESP_LOGD(TAG, "websocket sending with client: %p, message (%p size %ul): '%s', wait: %d", client, message_string, (message_string != NULL) ? strlen(message_string) : 0, (message_string != NULL) ? message_string : "", WEBSOCKET_WRITE_TIMEOUT);
	err = esp_websocket_client_send_text(client, message_string, strlen(message_string), pdMS_TO_TICKS(WEBSOCKET_WRITE_TIMEOUT));
	free(message_string);

	if(err == -1){
		ESP_LOGE(TAG, "Got websocket error when sending ocpp message");

		fail_active_call(&call, OCPPJ_ERROR_INTERNAL, "CP unable to send", NULL);
		xQueueReset(ocpp_active_call_queue);

		return -1;
	}

	last_call_timestamp = time(NULL);

	reset_heartbeat_timer();
	reset_message_timeout();

	return 0;
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

	if(enqueue_call_immediate(heartbeat_request, heartbeat_result_cb, heartbeat_error_cb, NULL, eOCPP_CALL_GENERIC) != 0){
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
		if(ocpp_is_connected()){
			ocpp_call_queue = xQueueCreate(5, sizeof(struct ocpp_call_with_cb *));
			ocpp_blocking_call_queue = xQueueCreate(1, sizeof(struct ocpp_call_with_cb *));
			ocpp_transaction_call_queue = xQueueCreate(MAX_TRANSACTION_QUEUE_SIZE, sizeof(struct ocpp_call_with_cb *));
			ocpp_active_call_queue = xQueueCreate(1, sizeof(struct ocpp_active_call));

			if(ocpp_call_queue == NULL || ocpp_transaction_call_queue == NULL || ocpp_blocking_call_queue == NULL || ocpp_active_call_queue == NULL){
				ESP_LOGE(TAG, "Unable to create call queues");
				goto error;
			}

			ocpp_active_call_lock_1 = xSemaphoreCreateBinary();
			if(ocpp_active_call_lock_1 == NULL){
				ESP_LOGE(TAG, "Unable to create semaphore");
				goto error;
			}

			status_notification_lock = xSemaphoreCreateMutex();
			if(status_notification_lock == NULL){
				ESP_LOGE(TAG, "Unable to create status notification lock");
				goto error;
			}

			if(xSemaphoreGive(ocpp_active_call_lock_1) != pdTRUE){
				ESP_LOGE(TAG, "Unable to open semaphore");
				goto error;
			}

			block_enqueue_call(0);
			block_sending_call(0);

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
	int remaining_call_count;
	if(send_next_call(&remaining_call_count) != 0){
		ESP_LOGE(TAG, "Unable to send boot message");
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
				if(send_next_call(&remaining_call_count) != 0){
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

cJSON * ocpp_task_get_diagnostics(){
	cJSON * res = cJSON_CreateObject();
	if(res == NULL){
		ESP_LOGE(TAG, "Unable to create ocpp diagnostics for task");
		return res;
	}

	cJSON_AddStringToObject(res, "b_chargebox_nr", boot_parameter_chargebox_nr);
	cJSON_AddStringToObject(res, "b_charge_point_model", boot_parameter_charge_point_model);
	cJSON_AddStringToObject(res, "b_cp_nr", boot_parameter_cp_nr);
	cJSON_AddStringToObject(res, "b_rcp_vendor", boot_parameter_cp_vendor);
	cJSON_AddStringToObject(res, "b_firm_ver", boot_parameter_firm_ver);
	cJSON_AddStringToObject(res, "b_iccid", boot_parameter_iccid);
	cJSON_AddStringToObject(res, "b_imsi", boot_parameter_imsi);
	cJSON_AddStringToObject(res, "b_meter_nr", boot_parameter_meter_nr);
	cJSON_AddStringToObject(res, "b_meter_type", boot_parameter_meter_type);

	cJSON_AddBoolToObject(res, "active_call", (ocpp_active_call_queue != NULL && !uxQueueSpacesAvailable(ocpp_active_call_queue)));
	cJSON_AddNumberToObject(res, "last_call_time", last_call_timestamp);
	cJSON_AddBoolToObject(res, "offline_enabled", offline_enabled);

	return res;
}

void fail_all_queued(const char * error_description){

	struct ocpp_active_call call = {0};

	if(ocpp_blocking_call_queue != NULL){
		UBaseType_t enqueued_count = uxQueueMessagesWaiting(ocpp_blocking_call_queue);
		for(size_t i = 0; i < enqueued_count; i++){
			if(xQueueReceive(ocpp_blocking_call_queue, &call.call, pdMS_TO_TICKS(1000)) == pdTRUE){
				call.retries = 0;
				fail_active_call(&call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
			}
		}
	}

	if(ocpp_transaction_call_queue != NULL){
		UBaseType_t enqueued_count = uxQueueMessagesWaiting(ocpp_transaction_call_queue);
		for(size_t i = 0; i < enqueued_count; i++){
			if(xQueueReceive(ocpp_transaction_call_queue, &call.call, pdMS_TO_TICKS(1000)) == pdTRUE){
				call.retries = 0;
				fail_active_call(&call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
			}
		}
	}

	if(ocpp_call_queue != NULL){
		UBaseType_t enqueued_count = uxQueueMessagesWaiting(ocpp_call_queue);
		for(size_t i = 0; i < enqueued_count; i++){
			if(xQueueReceive(ocpp_call_queue, &call.call, pdMS_TO_TICKS(1000)) == pdTRUE){
				call.retries = 0;
				fail_active_call(&call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
			}
		}
	}

	if(ocpp_active_call_queue != NULL){
		if(xQueueReceive(ocpp_active_call_queue, &call.call, 0) == pdTRUE){
			call.retries = 0;
			fail_active_call(&call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
		}
	}

	if(failed_transaction_call != NULL){
		failed_transaction_call->retries = 0;
		fail_active_call(failed_transaction_call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
	}

	if(awaiting_failed != NULL){
		awaiting_failed->retries = 0;
		fail_active_call(awaiting_failed, OCPPJ_ERROR_INTERNAL, error_description, NULL);
	}

}

void stop_ocpp(void){
	ESP_LOGW(TAG, "Closing web socket");

	block_enqueue_call(eOCPP_CALL_GENERIC | eOCPP_CALL_TRANSACTION_RELATED | eOCPP_CALL_BLOCKING);
	block_sending_call(eOCPP_CALL_GENERIC | eOCPP_CALL_TRANSACTION_RELATED | eOCPP_CALL_BLOCKING);

	/* if(esp_websocket_client_is_connected(client)){ */
	/* 	//Only awailable in newer versions of esp-idf */
	/* 	esp_websocket_client_close(client, pdMS_TO_TICKS(5000)); */
	/* } */
	if(client != NULL){
		esp_websocket_client_destroy(client); // Calls esp_websocket_client_stop internaly
		client = NULL;
	}

	fail_all_queued("Stopping ocpp");

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
		ocpp_transaction_call_queue = NULL;
	}

	if(ocpp_active_call_queue != NULL){
		vQueueDelete(ocpp_active_call_queue);
		ocpp_active_call_queue = NULL;
	}

	if(ocpp_active_call_lock_1 != NULL){
		vSemaphoreDelete(ocpp_active_call_lock_1);
		ocpp_active_call_lock_1 = NULL;
	}

	if(status_notification_lock != NULL){
		vSemaphoreDelete(status_notification_lock);
		status_notification_lock = NULL;
	}

	if(message_timeout_handle != NULL){
		if(xTimerDelete(message_timeout_handle, pdMS_TO_TICKS(500)) != pdTRUE){
			ESP_LOGE(TAG, "Unable to stop heartbeat timer");
		}else{
			message_timeout_handle = NULL;
		}
	}

	clean_listener();

	block_enqueue_call(0);
	block_sending_call(0);

	ESP_LOGW(TAG, "Web socket closed and state cleared");
	return;
}

int handle_ocpp_call(int * remaining_call_count_out){
	return send_next_call(remaining_call_count_out);
}
