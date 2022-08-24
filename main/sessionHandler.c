#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "sessionHandler.h"
#include "CLRC661.h"
#include "ppp_task.h"
#include "at_commands.h"
#include "zaptec_cloud_observations.h"
#include "network.h"
#include "protocol_task.h"
#include "zaptec_cloud_listener.h"
#include "DeviceInfo.h"
#include "chargeSession.h"
#include "storage.h"
#include "connectivity.h"
#include "apollo_ota.h"
#include "string.h"
#include "OCMF.h"
#include "freertos/event_groups.h"
#include "../components/ntp/zntp.h"
#include "../components/authentication/authentication.h"
#include "../components/i2c/include/i2cDevices.h"
#include "offline_log.h"
#include "offlineSession.h"
#include "offlineHandler.h"
#include "../components/audioBuzzer/audioBuzzer.h"

#include "ocpp_task.h"
#include "ocpp.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_reason.h"
#include "types/ocpp_authorization_status.h"
#include "types/ocpp_availability_type.h"
#include "types/ocpp_availability_status.h"
#include "types/ocpp_charge_point_status.h"
#include "types/ocpp_remote_start_stop_status.h"
#include "types/ocpp_charge_point_error_code.h"
#include "types/ocpp_ci_string_type.h"
#include "ocpp_listener.h"
#include "ocpp_task.h"

static const char *TAG = "SESSION        ";

#define RESEND_REQUEST_TIMER_LIMIT 90
#define OCMF_INTERVAL_TIME 3600
#define PULSE_INIT_TIME 10000

static char * completedSessionString = NULL;

TimerHandle_t signedMeterValues_timer;
static bool hasRemainingEnergy = false;
static bool hasCharged = false;

static uint32_t secSinceLastOCMFMessage = OCMF_INTERVAL_TIME; //Ensure we send a message at first occurence

//Send every clock aligned hour
void on_send_signed_meter_value()
{
	//If we have just synced with NTP and Timer event has caused redundant trip, return. Max 30 sec adjustment.
	if(secSinceLastOCMFMessage <= 30)
	{
		ESP_LOGW(TAG, "****** DOUBLE OCMF %d -> RETURNING ******", secSinceLastOCMFMessage);
		return;
	}

	secSinceLastOCMFMessage = 0;

	char OCMPMessage[220] = {0};
	time_t timeSec;
	double energy;

	enum CarChargeMode chargeMode = MCU_GetChargeMode();
	bool state_charging = (chargeMode == eCAR_CHARGING);
	bool state_log_empty = false;
	int publish_result = -1;

	if((state_charging == true) || (hasCharged == true))
	{
		hasRemainingEnergy = true;
		hasCharged = false;
	}

	if(state_charging || hasRemainingEnergy){
		// Sample energy now, dumping the log may be to slow to get the time aligned energy
		OCMF_SignedMeterValue_CreateNewOCMFMessage(OCMPMessage, &timeSec, &energy);
	}

	if(hasRemainingEnergy)
		ESP_LOGW(TAG, "### Set to report any remaining energy. RV=%f ###", energy);

	ESP_LOGI(TAG, "***** Clearing energy log *****");

	if(!isMqttConnected()){
		// Do not attempt sending data when we know that the system is offline
	}else if(attempt_log_send()==0){
		ESP_LOGI(TAG, "energy log empty");
		state_log_empty = true;
	}

	if ((state_charging && state_log_empty) || (hasRemainingEnergy && state_log_empty)){
		publish_result = publish_string_observation_blocked(
			SignedMeterValue, OCMPMessage, 10000
		);

		if(publish_result<0){
			append_offline_energy(timeSec, energy);
		}

	}else if(state_charging || hasRemainingEnergy){
		ESP_LOGI(TAG, "failed to empty log, appending new measure");
		append_offline_energy(timeSec, energy);
	}


	if(state_charging || hasRemainingEnergy){
		// CompletedSession-Log
		// Add to log late to increase chance of consistent logs across observation types

		//If hasRemainingEnergy, but disconnected -> don't add.
		if (chargeMode != eCAR_DISCONNECTED)
			OCMF_CompletedSession_AddElementToOCMFLog('T', timeSec, energy);
	}

	//If this is the case, remaining energy has been sent -> clear the flag
	if((state_charging == false) && (hasRemainingEnergy == true))
	{
		hasRemainingEnergy = false;
		ESP_LOGW(TAG, "### Cleared remaining energy flag ###");
	}

	//ESP_LOGE(TAG, "********** OCMF INTERVAL ***********");
}


/*void SetDataInterval(int newDataInterval)
{
	dataTestInterval = newDataInterval;
}*/

static bool authorizationRequired = true;
static bool pendingCloudAuthorization = false;
static char pendingAuthID[PREFIX_GUID]= {0}; //BLE- + GUID
static bool isAuthorized = false;

void SetPendingRFIDTag(char * pendingTag)
{
	strcpy(pendingAuthID, pendingTag);
}

void SetAuthorized(bool authFromCloud)
{
	isAuthorized = authFromCloud;

	if((isAuthorized == true) && (pendingAuthID[0] !='\0'))
	{
		chargeSession_SetAuthenticationCode(pendingAuthID);
		//Update session on file with RFID-info
		chargeSession_SaveUpdatedSession();
	}

	pendingCloudAuthorization = false;
	memset(pendingAuthID, 0, PREFIX_GUID);
}

void log_task_info(void){
	char task_info[40*15];

	// https://www.freertos.org/a00021.html#vTaskList
	vTaskList(task_info);
	ESP_LOGD(TAG, "[vTaskList:]\n\r"
	"name\t\tstate\tpri\tstack\tnum\tcoreid"
	"\n\r%s\n"
	, task_info);

	vTaskGetRunTimeStats(task_info);
	ESP_LOGD(TAG, "[vTaskGetRunTimeStats:]\n\r"
	"\rname\t\tabsT\t\trelT\trelT"
	"\n\r%s\n"
	, task_info);

	// memory info as extracted in the HAN adapter project:
	size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

	// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/heap_debug.html
	char formated_memory_use[256];
	snprintf(formated_memory_use, 256,
		"[MEMORY USE] (GetFreeHeapSize now: %d, GetMinimumEverFreeHeapSize: %d, heap_caps_get_free_size: %d)",
		xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize(), free_heap_size
	);
	ESP_LOGD(TAG, "%s", formated_memory_use);

	// heap_caps_print_heap_info(MALLOC_CAP_EXEC|MALLOC_CAP_32BIT|MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL|MALLOC_CAP_DEFAULT|MALLOC_CAP_IRAM_8BIT);
	heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

	publish_diagnostics_observation(formated_memory_use);
	ESP_LOGD(TAG, "log_task_info done");
}




bool startupSent = false;
bool setTimerSyncronization = false;

//static bool stoppedByCloud = false;

/*void sessionHandler_SetStoppedByCloud(bool stateFromCloud)
{
	stoppedByCloud = stateFromCloud;
	SetClearSessionFlag();
}*/

SemaphoreHandle_t ocmf_sync_semaphore;

void ocmf_sync_task(void * pvParameters){
	while(true){
		if( xSemaphoreTake( ocmf_sync_semaphore, portMAX_DELAY ) == pdTRUE ){
			ESP_LOGI(TAG, "triggered periodic sync with semaphore");
			on_send_signed_meter_value();
		}else{
			ESP_LOGE(TAG, "bad semaphore??");
		}
	}

}

void on_ocmf_sync_time(TimerHandle_t xTimer){
	xSemaphoreGive(ocmf_sync_semaphore);
}

//For diagnostics and developement
static float currentSetFromCloud = 0.0;
static int phasesSetFromCloud = 0;
void sessionHandler_HoldParametersFromCloud(float newCurrent, int newPhases)
{
	currentSetFromCloud = newCurrent;
	phasesSetFromCloud = newPhases;
}

//For diagnostics and developement
static void sessionHandler_PrintParametersFromCloud()
{
	float pilot = 0.0;
	float actualCurrentSet = 0.0;
	if(storage_Get_MaxInstallationCurrentConfig() < currentSetFromCloud)
	{
		pilot = storage_Get_MaxInstallationCurrentConfig() / 0.6;
		actualCurrentSet = storage_Get_MaxInstallationCurrentConfig();
	}
	else
	{
		pilot = currentSetFromCloud / 0.6;
		actualCurrentSet = currentSetFromCloud;
	}

	ESP_LOGI(TAG,"FromCloud: %2.1f A, MaxInst: %2.1f A -> Set: %2.1f A, Pilot: %2.1f %%   SetPhases: %d, OfflineCurrent: %2.1f A", currentSetFromCloud, storage_Get_MaxInstallationCurrentConfig(), actualCurrentSet, pilot, phasesSetFromCloud, storage_Get_DefaultOfflineCurrent());
}

static bool offlineMode = false;
bool SessionHandler_IsOfflineMode()
{
	return offlineMode;
}

static bool stackDiagnostics = false;

static bool OCMFHighInterval = false;
void SessionHandler_SetOCMFHighInterval()
{
	ESP_LOGW(TAG, "Setting 60 sec interval");
	TickType_t periode = pdMS_TO_TICKS(60*1000);
	xTimerChangePeriod(signedMeterValues_timer, periode, portMAX_DELAY);
	setTimerSyncronization = false;
	OCMFHighInterval = true;
}

static bool logCurrents = false;
static uint16_t logCurrentsCounter = 0;
void SessionHandler_SetLogCurrents()
{
	if(logCurrents == false)
	{
		logCurrents = true;
		logCurrentsCounter = 0;
	}
	else
	{
		logCurrents = false;
	}
}

enum ocpp_cp_status_id ocpp_old_state = eOCPP_CP_STATUS_UNAVAILABLE;

/*
 * transaction_id is given by the CS as a result of StartTransaction.req in ocpp 1.6.
 * in ocpp 2.1 it is given by the CP.
 *
 * If a transaction is started offline, ocpp 1.6 does not give it an id before the CP has come online and recieved
 * the result from the StartTransaction.req. If CP fails to recieve the id, it will be unable to create a valid StopTransaction.req
 * as it requires an id. Transaction related MeterValues will still be sendt but can not be associated with a transaction.
 *
 * The current implementation of the ocpp component expects messages to be enqueued and only allows valid messages to be created.
 * to allow the construction of MeterValue.req and StopTransaction.req when StartTransaction.conf has not been recieved, a random id
 * will be created before sending the StartTransaction.req. This id will then be replaced by the valid id when the confirmation is
 * recieved. This will cause issues if a confirmation is never recieved. What should happen if id is not recieved is not specified
 * by ocpp 1.6.
 */
int * transaction_id = NULL;
time_t transaction_start = 0;
int meter_start = 0;
bool pending_change_availability = false;
bool ocpp_finishing_session = false; // Used to differentiate between eOCPP_CP_STATUS_FINISHING and eOCPP_CP_STATUS_PREPARING
uint8_t pending_change_availability_state;
time_t preparing_started = 0;

bool sessionHandler_OcppTransactionIsActive(uint connector_id){
	if(connector_id == 1){
		return (transaction_start != 0);
	}else{
		return false;
	}
}

int * sessionHandler_OcppGetTransactionId(uint connector_id){
	if(sessionHandler_OcppTransactionIsActive(connector_id)){
		return transaction_id;
	}else{
		return NULL;
	}
}

time_t sessionHandler_OcppTransactionStartTime(){
	return transaction_start;
}

void sessionHandler_OcppStopTransaction(const char * reason){
	ESP_LOGI(TAG, "Stopping charging");

	MessageType ret = MCU_SendCommandId(CommandStopCharging);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU stop charging command OK");
	}
	else
	{
		ESP_LOGE(TAG, "MCU stop charging final command FAILED");
	}
	chargeSession_SetStoppedReason(reason);
}


static void start_transaction_response_cb(const char * unique_id, cJSON * payload, void * cb_data){

	bool is_current_transaction = false;
	if(cJSON_HasObjectItem(payload, "transactionId")){
		/*
		 * Cloud sets session id whilest in requesting state, ocpp sets transaction id during charge state
		 * if cloud sets the id first, we should not update it as it would confuse the cloud.
		 */
		int tmp_id = *(int*)cb_data;
		int received_id = cJSON_GetObjectItem(payload, "transactionId")->valueint;

		if(transaction_id != NULL && *transaction_id == tmp_id){
			*transaction_id = received_id;
			is_current_transaction = true;
			ESP_LOGI(TAG, "Sat valid transaction_id for current transaction");
		}

		esp_err_t err = offlineSession_UpdateTransactionId_ocpp(tmp_id, received_id);
		if(err == ESP_OK){
			ESP_LOGI(TAG, "Sat valid transaction_id for stored transaction");

		}else if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGI(TAG, "No stored transaction for transaction_id");

		}else{
			ESP_LOGE(TAG, "Failed to set valid id for stored transaction");
		}

		if(ocpp_update_enqueued_transaction_id(tmp_id, received_id) != 0){
			ESP_LOGE(TAG, "Unable to update enqueued transaction id");
		}

		// Only set the transaction id if cloud did not yet and response is related to active transaction
		if(strstr(chargeSession_GetSessionId(), "-") == NULL && is_current_transaction){
			//If session id is persisted from a previous session, then this might still be valid as ocpp will use the transaction id instead, but this should be changed.
			char transaction_id_str[37]; // when not using ocpp directly, session id is UUID

			snprintf(transaction_id_str, sizeof(transaction_id_str), "%d", *transaction_id);

			chargeSession_SetSessionIdFromCloud(transaction_id_str);
		}
	}else{
		ESP_LOGE(TAG, "Recieved start transaction response lacking 'transactionId'");
	}

	if(cJSON_HasObjectItem(payload, "idTagInfo")){
		cJSON * id_tag_info = cJSON_GetObjectItem(payload, "idTagInfo");

		if(cJSON_HasObjectItem(id_tag_info, "status")){
			const char * status = cJSON_GetObjectItem(id_tag_info, "status")->valuestring;
			ESP_LOGI(TAG, "Central system returned status %s", status);

			if(storage_Get_ocpp_stop_transaction_on_invalid_id() && strcmp(status, OCPP_AUTHORIZATION_STATUS_ACCEPTED) != 0){
				ESP_LOGW(TAG, "Transaction not Autorized");
				if(is_current_transaction){
					MessageType ret = MCU_SendCommandId(CommandStopCharging);
					if(ret == MsgCommandAck)
					{
						ESP_LOGI(TAG, "MCU stop charging command OK");
					}
					else
					{
						ESP_LOGE(TAG, "MCU stop charging final command FAILED");
						//TODO: Handle error. If this occurs there might be charging without valid token or payment
					}
					chargeSession_SetStoppedReason(OCPP_REASON_DE_AUTHORIZED);
					SetAuthorized(false);
				}else{
					ESP_LOGE(TAG, "An inactive transaction was deauthorized");
				}
			}

			if(cJSON_HasObjectItem(id_tag_info, "parentIdTag")){
				cJSON * parent_id_json = cJSON_GetObjectItem(id_tag_info, "parentIdTag");
				if(cJSON_IsString(parent_id_json) && is_ci_string_type(parent_id_json->valuestring, 20)){
					ESP_LOGI(TAG, "Adding parent id to current charge session");
					chargeSession_SetParentId(parent_id_json->valuestring);
				}else{
					ESP_LOGE(TAG, "Recieved start transaction with invalid parent id");
				}
			}
		}
	}else{
		ESP_LOGE(TAG, "Recieved start transaction response lacking 'idTagInfo'");
	}
}

static void stop_transaction_response_cb(const char * unique_id, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Stop transaction response success");
}

bool pending_ocpp_authorize = false;

static void error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data){
	const char * action = (cb_data != NULL) ? (const char *) cb_data : "No cb";

	if(strcmp(action, "authorize"))
		pending_ocpp_authorize = false;

	if(unique_id != NULL && error_code != NULL && error_description != NULL){
		if(error_details == NULL){
			ESP_LOGE(TAG, "[%s|%s]: (%s) '%s'", action, unique_id, error_code, error_description);
		}else{
			char * details = cJSON_Print(error_details);
			if(details != NULL){
				ESP_LOGE(TAG, "[%s|%s]: (%s) '%s' \nDetails:\n %s", action, unique_id, error_code, error_description, details);
				free(details);
			}
		}
	}else{
		ESP_LOGE(TAG, "Error reply from ocpp '%s' call", action);
	}
}

static void start_transaction_error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data){
        int * failed_transaction_id = (int *) cb_data;

	ESP_LOGE(TAG, "Start transaction request failed for temporary id %d", *failed_transaction_id);

	if(error_code != NULL){
		if(error_description != NULL){
			if(error_details != NULL){
				char * details = cJSON_Print(error_details);
				if(details != NULL){
					ESP_LOGE(TAG, "[%s]: (%s) '%s' \nDetails:\n %s", unique_id, error_code, error_description, details);
					free(details);
				}
			}else{
				ESP_LOGE(TAG, "[%s]: (%s) '%s'", unique_id, error_code, error_description);
			}
		}else{
			ESP_LOGE(TAG, "[%s]: (%s)", unique_id, error_code);
		}
	}
}

#define MAX_STOP_TXN_METER_VALUES 512
static struct ocpp_meter_value_list * current_meter_values = NULL;
static size_t current_meter_values_length = 0;
bool current_meter_values_failed = true;

/**
 * This function is used to save transaction related meter values that are meant to be used in StopTransaction.req.
 * Ocpp does not specify a limit to how many meter values may be in a StopTransaction.req. Too prevent crashes due to no
 * more memory, we limit it by MAX_STOP_TXN_METER_VALUES.
 *
 * If the limit is reached we indicate that current meter values failed, delete the meter values and prevent saving new meter
 * values until the next transaction starts. A status notification is sendt to inform CS of the issue.
 */
void sessionHandler_OcppTransferMeterValues(uint connector_id, struct ocpp_meter_value_list * values, size_t length){
	if(connector_id != 1 || sessionHandler_OcppTransactionIsActive(connector_id) == false){
		ESP_LOGE(TAG, "sessionHandler got notified of meter values without ongoing transaction, value recieved too late and transactionData might be wrong");
		ocpp_meter_list_delete(values);
		return;
	}

	if(current_meter_values == NULL){
		if(current_meter_values_failed == false){
			if(length > MAX_STOP_TXN_METER_VALUES){
				ESP_LOGE(TAG, "First meter values list already too large for stop transaction");
				goto error;
			}

			current_meter_values = ocpp_create_meter_list();
			if(current_meter_values == NULL){
				ESP_LOGE(TAG, "Unable to create meter value list to store transaction data");
				goto error;
			}

			current_meter_values_length = 0;
		}else{
			ESP_LOGW(TAG, "New meter values for stop transaction after failed");
			goto error;
		}
	}

	if(current_meter_values_length + length < MAX_STOP_TXN_METER_VALUES){
		struct ocpp_meter_value_list * last_ptr = ocpp_meter_list_get_last(current_meter_values);
		if(last_ptr->value != NULL){
			last_ptr->next = ocpp_create_meter_list();
			if(last_ptr->next == NULL){
				ESP_LOGE(TAG, "Unable to allocate space for StopTxnData");
				goto error;
			}

			last_ptr = last_ptr->next;
		}

		last_ptr->value = values->value;
		last_ptr->next = values->next;

		current_meter_values_length += length;
	}else{
		ESP_LOGE(TAG, "Too many meter values for stop transaction");
		current_meter_values_failed = true;

		ocpp_meter_list_delete(current_meter_values);
		current_meter_values = NULL;

		//TODO: Inform CS of failure
		goto error;
	}

	ESP_LOGW(TAG, "Current meter values length: %d (MAX: %d)", current_meter_values_length, MAX_STOP_TXN_METER_VALUES);
	return;
error:
	ocpp_meter_list_delete(values);
}

TimerHandle_t sample_handle = NULL;

static void sample_meter_values(){
	ESP_LOGI(TAG, "Starting periodic meter values");

	uint connector = 1;
	handle_meter_value(eOCPP_CONTEXT_SAMPLE_PERIODIC,
			storage_Get_ocpp_meter_values_sampled_data(), storage_Get_ocpp_stop_txn_sampled_data(),
			transaction_id, &connector, 1);
}

static void start_sample_interval(){
	ESP_LOGI(TAG, "Starting sample interval");
	current_meter_values_failed = false;

	sample_handle = xTimerCreate("Ocpp sample",
				pdMS_TO_TICKS(storage_Get_ocpp_meter_value_sample_interval() * 1000),
				pdTRUE, NULL, sample_meter_values);

	if(sample_handle == NULL){
		ESP_LOGE(TAG, "Unable to create sample handle");
	}else{
		if(xTimerStart(sample_handle, pdMS_TO_TICKS(200)) != pdPASS){
			ESP_LOGE(TAG, "Unable to start sample interval");
		}else{
			ESP_LOGI(TAG, "Started sample interval");
		}
	}
	save_interval_measurands(eOCPP_CONTEXT_TRANSACTION_BEGIN);
	ESP_LOGW(TAG, "EXIT");
}

static void stop_sample_interval(){
	if(sample_handle == NULL){
		return;
	}

	ESP_LOGI(TAG, "Stopping sample interval");

	xTimerDelete(sample_handle, pdMS_TO_TICKS(200));
	sample_handle = NULL;

	uint connector = 1;
	handle_meter_value(eOCPP_CONTEXT_TRANSACTION_END,
			storage_Get_ocpp_meter_values_sampled_data(), storage_Get_ocpp_stop_txn_sampled_data(),
			transaction_id, &connector, 1);
}

void stop_transaction(){ // TODO: Use (required) StopTransactionOnEVSideDisconnect
	stop_sample_interval();

	int meter_stop = meter_start + (chargeSession_Get().Energy * 1000);

	if(chargeSession_Get().SessionId[0] == '\0'){ // if session has been cleared the meter value should already be in storage
		meter_stop = floor(storage_update_accumulated_energy(0.0) * 1000);
		ESP_LOGW(TAG, "Meter stop read from file: %d", meter_stop);
	}else{
		ESP_LOGW(TAG, "Meter stop read from charge session");
	}

	time_t timestamp = time(NULL);
	char * stop_token = (chargeSession_Get().StoppedByRFID) ? chargeSession_Get().StoppedById : NULL;

	if(strcmp(chargeSession_Get().StoppedReason, OCPP_REASON_EV_DISCONNECT) == 0 &&
		storage_Get_ocpp_stop_transaction_on_ev_side_disconnect()){

		/*
		 * Ocpp 1.6 specify that a status notification with state finishing, no error, and info about ev disconnect.
		 * SHOULD be sendt to the CS.
		 * The motivation for this is unclear as the same information is given in the stop transaction and the
		 * specification also allows transition from charging/suspended to available (not just finishing).
		 * Finishing seems to be intended for situation where user action is required before new transaction or new user, yet
		 * we must inform CS of finishing before entering available.
		 */
		ocpp_send_status_notification(eOCPP_CP_STATUS_FINISHING, OCPP_CP_ERROR_NO_ERROR, "EV side disconnected");
	}

	cJSON * response  = ocpp_create_stop_transaction_request(stop_token, meter_stop, timestamp, transaction_id,
								chargeSession_Get().StoppedReason, current_meter_values);

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create stop transaction request");
	}else{
		int err = enqueue_call(response, stop_transaction_response_cb, error_cb, "stop", eOCPP_CALL_TRANSACTION_RELATED);
		if(err != 0){
			ESP_LOGE(TAG, "Unable to enqueue stop transaction request, storing stop transaction on file");
			esp_err_t err = offlineSession_SaveStopTransaction_ocpp(*transaction_id, transaction_start, stop_token, meter_stop,
										timestamp, chargeSession_Get().StoppedReason);

			if(err != ESP_OK){
				ESP_LOGE(TAG, "Failed to save stop transaction to file");
			}

			if(current_meter_values != NULL){
				size_t meter_buffer_length;
				unsigned char * meter_buffer = ocpp_meter_list_to_contiguous_buffer(current_meter_values, true, &meter_buffer_length);
				if(meter_buffer == NULL){
					ESP_LOGE(TAG, "Could not create meter value as string for storing stop transaction data");
				}else{
					ESP_LOGI(TAG, "Storing stop transaction data as short string");
					if(offlineSession_SaveNewMeterValue_ocpp(*transaction_id, transaction_start, meter_buffer, meter_buffer_length) != ESP_OK){
						ESP_LOGE(TAG, "Unable to save stop transaction data");
					}
					free(meter_buffer);
				}
			}
		}
	}

	ocpp_meter_list_delete(current_meter_values);
	current_meter_values = NULL;

	free(transaction_id);
	transaction_start = 0;
	transaction_id = NULL;
}

void start_transaction(){
	transaction_start = time(NULL);
	meter_start = floor(storage_update_accumulated_energy(0.0) * 1000);
	cJSON * start_transaction  = ocpp_create_start_transaction_request(1, chargeSession_Get().AuthenticationCode, meter_start, -1, transaction_start);

	transaction_id = malloc(sizeof(int));
	if(transaction_id == NULL){
		ESP_LOGE(TAG, "Unable to create buffer for transaction id");
		return;
	}

	*transaction_id = esp_random();

	if(start_transaction == NULL){
		ESP_LOGE(TAG, "Unable to create start transaction request");
	}else{
		int err = enqueue_call(start_transaction, start_transaction_response_cb, start_transaction_error_cb,
				transaction_id, eOCPP_CALL_TRANSACTION_RELATED);
		if(err != 0){
			ESP_LOGE(TAG, "Unable to enqueue start transaction request, storing on file");
			offlineSession_SaveStartTransaction_ocpp(*transaction_id, transaction_start, 1,
								chargeSession_Get().AuthenticationCode, meter_start, NULL);
		}
	}

	if(storage_Get_ocpp_meter_value_sample_interval() > 0)
		start_sample_interval();
}

static void authorize_response_cb(const char * unique_id, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Got auth response");
	pending_ocpp_authorize = false;

	if(cJSON_HasObjectItem(payload, "idTagInfo")){
		cJSON * id_tag_info = cJSON_GetObjectItem(payload, "idTagInfo");

		if(cJSON_HasObjectItem(id_tag_info, "status")){
			char * status = cJSON_GetObjectItem(id_tag_info, "status")->valuestring;

			if(strcmp(status, OCPP_AUTHORIZATION_STATUS_ACCEPTED) == 0){
				ESP_LOGI(TAG, "Authorized to start transaction");

				audio_play_nfc_card_accepted();
				MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
				if(ret != MsgCommandAck)
				{
					ESP_LOGE(TAG, "Unable to grant authorization to MCU");
				}
				else{
					ESP_LOGI(TAG, "Authorization granted ok");
					SetAuthorized(true);
				}
			}else{
				ESP_LOGW(TAG, "Transaction is not authorized");
				audio_play_nfc_card_denied();
				MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
				if(ret == MsgCommandAck)
				{
					ESP_LOGI(TAG, "MCU authorization denied command OK");
				}
				else
				{
					ESP_LOGI(TAG, "MCU authorization denied command FAILED");
				}

				SetAuthorized(false);
			}
		}else{
			ESP_LOGE(TAG, "Authorization tag info lacks required 'status'");
		}
	}else{
		ESP_LOGE(TAG, "Authorize response lacks required 'idTagInfo'");
	}
}

void authorize(struct TagInfo tag){
	//TODO: handle error to deny authorization or retry if timed out.
	if(storage_Get_ocpp_local_pre_authorize()){
		ESP_LOGI(TAG, "Attempting local pre authorization");

		if(authentication_CheckId(tag) == 1){
			ESP_LOGI(TAG, "Local authentication accepted");
			authentication_Execute(tag.idAsString);

			SetPendingRFIDTag(tag.idAsString);
			SetAuthorized(true);

			NFCTagInfoClearValid();
			return;
		}
	}

	ESP_LOGI(TAG, "Authenticating with central system");
	cJSON * authorization = ocpp_create_authorize_request(tag.idAsString);
	if(authorization == NULL){
		ESP_LOGE(TAG, "Unable to create authorization request");
	}else{
		int err = enqueue_call(authorization, authorize_response_cb, error_cb, "authorize", eOCPP_CALL_GENERIC);
		if(err != 0){
			ESP_LOGE(TAG, "Unable to enqueue authorization request");
		}else{
			MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
			if(ret == MsgWriteAck)
				ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
			else
				ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

			pending_ocpp_authorize = true;
			SetPendingRFIDTag(tag.idAsString);
		}
	}

	NFCTagInfoClearValid();
}

static enum  SessionResetMode sessionResetMode = eSESSION_RESET_NONE;

void authorize_and_stop_transaction(const char * id_tag){
		ESP_LOGI(TAG, "Authorized to stop transaction");

		audio_play_nfc_card_accepted();
		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "Authorization granted ok");
		}
		else{
			ESP_LOGE(TAG, "Unable to grant authorization to MCU");
		}

		ret = MCU_SendCommandId(CommandStopCharging);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "MCU stop charging OK");
			sessionResetMode = eSESSION_RESET_STOP_SENT;
		}
		else
		{
			ESP_LOGE(TAG, "MCU stop charging Failed");
		}
		chargeSession_SetStoppedByRFID(true, id_tag);
}

void authorize_stop_response_cb(const char * unique_id, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received authorization response for stop transaction");
	pending_ocpp_authorize = false;

	if(chargeSession_Get().parent_id[0] == 0){
		ESP_LOGE(TAG, "Chargesession lacks parent id, authorization request was unnecessary or charge session changed");
		goto denied;
	}

	if(cJSON_HasObjectItem(payload, "idTagInfo")){
		cJSON * id_tag_info_json = cJSON_GetObjectItem(payload, "idTagInfo");

		if(cJSON_HasObjectItem(id_tag_info_json, "status")){

			cJSON * status_json = cJSON_GetObjectItem(id_tag_info_json, "status");
			if(!cJSON_IsString(status_json) || strcmp(status_json->valuestring, OCPP_AUTHORIZATION_STATUS_ACCEPTED) != 0){
				ESP_LOGW(TAG, "Status not accepted %s", status_json->valuestring );
				goto denied;
			}
		}else{
			goto denied;
		}

		if(cJSON_HasObjectItem(id_tag_info_json, "parentIdTag")){

			cJSON * parent_id_json = cJSON_GetObjectItem(id_tag_info_json, "parentIdTag");
			if(!cJSON_IsString(parent_id_json) || !is_ci_string_type(parent_id_json->valuestring, 20)
				|| strcmp(parent_id_json->valuestring, chargeSession_Get().parent_id) != 0){

				ESP_LOGW(TAG, "parent id tag does not match transaction parent");
			}else{
				authorize_and_stop_transaction((char *)cb_data);
				free(cb_data);
				return;
			}
		}
	}

denied:
	free(cb_data);
	ESP_LOGW(TAG, "Stop transaction not authorized");
	audio_play_nfc_card_denied();
	MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU authorization denied command OK");
	}
	else
	{
		ESP_LOGI(TAG, "MCU authorization denied command FAILED");
	}
}

void authorize_stop_error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data){

	error_cb(unique_id, error_code, error_description, error_details, "authorize");

	ESP_LOGW(TAG, "Stop transaction not authorized: %s", (char *)cb_data);
	free(cb_data);

	audio_play_nfc_card_denied();
	MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU authorization denied command OK");
	}
	else
	{
		ESP_LOGI(TAG, "MCU authorization denied command FAILED");
	}
}

void authorize_stop(const char * presented_id_tag)
{
	struct ChargeSession charge_session = chargeSession_Get();

	if(strcmp(charge_session.AuthenticationCode, presented_id_tag) == 0){
		ESP_LOGI(TAG, "Transaction id tag is same as presented id tag");
		authorize_and_stop_transaction(presented_id_tag);
		return;
	}

	if(charge_session.parent_id[0] != '\0'){

		if(storage_Get_ocpp_local_pre_authorize()){
			ESP_LOGI(TAG, "Attemting local stop authorization");
			if(authentication_check_parent(presented_id_tag, charge_session.parent_id)){
				ESP_LOGI(TAG, "Transaction id tag is in same group as presented id tag");
				authorize_and_stop_transaction(presented_id_tag);
				return;
			}
		}


		ESP_LOGI(TAG, "Requeting presented id tag from central system");
		cJSON * authorization = ocpp_create_authorize_request(presented_id_tag);
		if(authorization != NULL){
			char * id_tag_buffer = malloc(sizeof(char) * 21);
			if(id_tag_buffer == NULL){
				ESP_LOGE(TAG, "Unable to allocate id tag buffer");
				goto denied;
			}
			strncpy(id_tag_buffer, presented_id_tag, 20);
			id_tag_buffer[20] = '\0';

			int err = enqueue_call(authorization, authorize_stop_response_cb, authorize_stop_error_cb, id_tag_buffer, eOCPP_CALL_GENERIC);
			if(err == 0){
				MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
				if(ret == MsgWriteAck)
					ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
				else
					ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

				pending_ocpp_authorize = true;
				return;
			}else{
				ESP_LOGE(TAG, "Unable to enqueue authorization request");
				free(authorization);
				free(id_tag_buffer);
			}
		}else{
			ESP_LOGE(TAG, "Unable to create authorize request");
		}
	}
	else{
		ESP_LOGW(TAG, "Charge session has no parent id to check");
	}
denied:
	audio_play_nfc_card_denied();
	if(MCU_SendCommandId(CommandAuthorizationDenied) == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU authorization denied command OK");
	}
	else
	{
		ESP_LOGI(TAG, "MCU authorization denied command FAILED");
	}
}

// Index in is equivalent to integer used to identify column in table in ocpp protocol specification section on status notification -1
// Index is used as [from state][to state]
bool want_status_notification[9][9] = {
	{false, false, true,  false, false, false, false, true,  true},
	{false, false, true,  false, false, false, false, false, true},
	{true,  false, false, false, false, false, false, true,  true},
	{true,  false, false, false, false, false, false, true,  true},
	{true,  true,  false, false, false, false, false, true,  true},
	{true,  false, false, false, false, false, false, true,  true},
	{false, false, false, false, false, false, false, true,  true},
	{true,  true,  true,  true,  true,  false, false, false, true},
	{false, false, true,  false, false, false, false, false, false},
};

enum ocpp_cp_status_id saved_state = eOCPP_CP_STATUS_UNAVAILABLE;
void sessionHandler_OcppSaveState(){
	saved_state = ocpp_old_state;
}

bool sessionHandler_OcppStateHasChanged(){
	return saved_state != ocpp_old_state;
}

void sessionHandler_OcppSendState(){
	ocpp_send_status_notification(ocpp_old_state, OCPP_CP_ERROR_NO_ERROR, NULL);
}

static int change_availability(uint8_t is_operative){
	MessageType ret = MCU_SendUint8Parameter(ParamIsEnabled, is_operative);
	if(ret == MsgWriteAck)
	{
		storage_Set_IsEnabled(is_operative);
		storage_SaveConfiguration();

		ESP_LOGW(TAG, "Availability changed successfully to %d", is_operative);
		return 0;
	}
	else
	{
		ESP_LOGE(TAG, "Unable to change availability to %d", is_operative);
		return -1;
	}
}

// See transition table in section on Status notification in ocpp 1.6 specification
static enum ocpp_cp_status_id get_ocpp_state(){

	// The state returned by MCU does not by itself indicate if it isEnabled/operable, so we check storage first.
	// We also require the the charger to be 'Accepted by central system' (optional) see 4.2.1. of the ocpp 1.6 specification
	if(storage_Get_IsEnabled() == 0 || get_registration_status() != eOCPP_REGISTRATION_ACCEPTED){
		return eOCPP_CP_STATUS_UNAVAILABLE;
	}

	enum CarChargeMode charge_mode = MCU_GetChargeMode();

	switch(MCU_GetChargeOperatingMode()){
	case CHARGE_OPERATION_STATE_UNINITIALIZED:
		return eOCPP_CP_STATUS_UNAVAILABLE;

	case CHARGE_OPERATION_STATE_DISCONNECTED:
		/*
		 * TODO: The specification states:
		 * "When a Charge Point is configured with StopTransactionOnEVSideDisconnect set to false,
		 * a transaction is running and the EV becomes disconnected on EV side,
		 * then a StatusNotification.req with the state: SuspendedEV SHOULD be send to the Central System,
		 * with the 'errorCode' field set to: 'NoError'. The Charge Point SHOULD add additional information
		 * in the 'info' field, Notifying the Central System with the reason of suspension:
		 * 'EV side disconnected'. The current transaction is not stopped."
		 */

		if(isAuthorized || pending_ocpp_authorize){
			return eOCPP_CP_STATUS_PREPARING;

		}else{
			return eOCPP_CP_STATUS_AVAILABLE;
		}

	case CHARGE_OPERATION_STATE_REQUESTING: // TODO: Add support for transition B6
		if(ocpp_finishing_session // not transitioning away from FINISHED
			|| ocpp_old_state == eOCPP_CP_STATUS_CHARGING // transition C6
			|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EV // transition D6
			|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EVSE){ // transition E6

		        return eOCPP_CP_STATUS_FINISHING;
		}else{ // Else it must be transition A2, F2, G2, H2 or not transitioning away from perparing
			return eOCPP_CP_STATUS_PREPARING;
		}

	case CHARGE_OPERATION_STATE_ACTIVE:
		return eOCPP_CP_STATUS_AVAILABLE;

	case CHARGE_OPERATION_STATE_CHARGING:
		return eOCPP_CP_STATUS_CHARGING;

	case CHARGE_OPERATION_STATE_STOPPING:
		return eOCPP_CP_STATUS_FINISHING;

	case CHARGE_OPERATION_STATE_PAUSED:
		if(charge_mode == eCAR_CHARGING){

			return eOCPP_CP_STATUS_SUSPENDED_EVSE;
		}else{
			return eOCPP_CP_STATUS_SUSPENDED_EV;
		}
	case CHARGE_OPERATION_STATE_STOPPED:
		return eOCPP_CP_STATUS_AVAILABLE;

	case CHARGE_OPERATION_STATE_WARNING:
		return eOCPP_CP_STATUS_FAULTED;
	default:
		ESP_LOGE(TAG, "Unexpected value of MCU charger operating mode");
		return eOCPP_CP_STATUS_FAULTED;
	}

}

static void remote_start_transaction_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to strat transaction");

	if(payload == NULL || !cJSON_HasObjectItem(payload, "idTag")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'idTag' field", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	int connector_id = 1;
	if(cJSON_HasObjectItem(payload, "connectorId")){
		cJSON * connector_id_json = cJSON_GetObjectItem(payload, "connectorId");
		if(cJSON_IsNumber(connector_id_json)){
			connector_id = connector_id_json->valueint;
		}else{
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'connectorId' to be interger type", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
			}else{
				send_call_reply(ocpp_error);
				cJSON_Delete(ocpp_error);
			}
			return;
		}

		if(connector_id <= 0 || connector_id > storage_Get_ocpp_number_of_connectors()){
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "Expected 'connectorId' to identify an existing connector", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error property constraint violation");
			}else{
				send_call_reply(ocpp_error);
				cJSON_Delete(ocpp_error);
			}
			return;

		}
	}

	cJSON * id_tag_json = cJSON_GetObjectItem(payload, "idTag");
	if(!cJSON_IsString(id_tag_json) || !is_ci_string_type(id_tag_json->valuestring, 20)){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'idTag' to be CiSstring20Type", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;

	}

	bool accept_request = get_ocpp_state() == eOCPP_CP_STATUS_PREPARING;
	cJSON * response;
	if(accept_request){
		response = ocpp_create_remote_start_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_ACCEPTED);
	}else{
		response = ocpp_create_remote_start_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_REJECTED);
	}

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create remote start transaction response");
		return;
	}else{
		send_call_reply(response);
		cJSON_Delete(response);
	}

	if(!accept_request)
		return;

	struct TagInfo tag ={
		.tagIsValid = true,
	};
	strcpy(tag.idAsString, id_tag_json->valuestring);

	if(storage_Get_ocpp_authorize_remote_tx_requests()){
		authorize(tag);
	}
}

static void remote_stop_transaction_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to remote stop transaction");
	if(payload == NULL || !cJSON_HasObjectItem(payload, "transactionId")){
		ESP_LOGW(TAG, "Request has invalid formation");
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'transactionId' field", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * transaction_id_json = cJSON_GetObjectItem(payload, "transactionID");
	if(!cJSON_IsNumber(transaction_id_json)){
		ESP_LOGW(TAG, "Request has invalid type");
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected transactionId to be integer", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * response;
	bool stop_charging = false;
	if(transaction_id != NULL && *transaction_id == transaction_id_json->valueint){
		ESP_LOGI(TAG, "Stop request id matches ongoing transaction id");
		stop_charging = true;
		response = ocpp_create_remote_stop_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_ACCEPTED);
	}
	else{
		ESP_LOGW(TAG, "Stop request id does not match any ongoing transaction id");
		response = ocpp_create_remote_stop_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_REJECTED);
	}

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create remote stop transaction confirmation");
	}
	else{
		send_call_reply(response);
		cJSON_Delete(response);
	}

	if(stop_charging){
		MessageType ret = MCU_SendCommandId(CommandStopCharging);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "MCU stop charging OK");
			sessionResetMode = eSESSION_RESET_STOP_SENT;
		}
		else
		{
			ESP_LOGE(TAG, "MCU stop charging Failed");
		}
	}

	SetAuthorized(false);
	chargeSession_SetStoppedReason(OCPP_REASON_REMOTE);
	//TODO: "[...]and, if applicable, unlock the connector"
}

static void change_availability_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to change availability");
	pending_change_availability = false;

	if(payload == NULL || !cJSON_HasObjectItem(payload, "connectorId") || !cJSON_HasObjectItem(payload, "type")){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'connectorId' and 'type' fields", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	cJSON * connector_id_json = cJSON_GetObjectItem(payload, "connectorId");
	cJSON * type_json = cJSON_GetObjectItem(payload, "type");

	if(!cJSON_IsNumber(connector_id_json) || !cJSON_IsString(type_json) || !ocpp_validate_enum(type_json->valuestring, 2,
													OCPP_AVAILABILITY_TYPE_INOPERATIVE,
													OCPP_AVAILABILITY_TYPE_OPERATIVE) == 0){

		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'connectorId' to be integer and 'type' to be AvailabilityType", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	if(connector_id_json->valueint < 0 || connector_id_json->valueint > storage_Get_ocpp_number_of_connectors()){
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "Expected 'connectorId' to identify a valid connector", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
		}else{
			send_call_reply(ocpp_error);
			cJSON_Delete(ocpp_error);
		}
		return;
	}

	uint8_t new_operative = (strcmp(type_json->valuestring, OCPP_AVAILABILITY_TYPE_INOPERATIVE) == 0) ? 0 : 1;
	char availability_status[16] = "";
	uint8_t old_is_enabled = storage_Get_IsEnabled();

	if(new_operative != old_is_enabled)
	{
		enum ocpp_cp_status_id ocpp_state = get_ocpp_state(MCU_GetChargeOperatingMode(), MCU_GetChargeMode());
		if((ocpp_state == eOCPP_CP_STATUS_AVAILABLE && new_operative == 0) || ocpp_state == eOCPP_CP_STATUS_UNAVAILABLE){
			if(change_availability(new_operative) == 0)
			{
				strcpy(availability_status, OCPP_AVAILABILITY_STATUS_ACCEPTED);
			}
			else
			{
				cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Unable to update availability", NULL);
				if(ocpp_error == NULL){
					ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
				}else{
					send_call_reply(ocpp_error);
					cJSON_Delete(ocpp_error);
				}
				return;
			}
		}
		// "When a transaction is in progress Charge Point SHALL respond with availability status 'Scheduled' to indicate that it is scheduled to occur after the transaction has finished"
		else{
			pending_change_availability = true;
			pending_change_availability_state = new_operative;
			strcpy(availability_status, OCPP_AVAILABILITY_STATUS_SCHEDULED);
		}
	}else{
		strcpy(availability_status, OCPP_AVAILABILITY_STATUS_ACCEPTED);
	}

	if(strlen(availability_status) != 0){
		cJSON * response = ocpp_create_change_availability_confirmation(unique_id, availability_status);
		if(response == NULL){
			ESP_LOGE(TAG, "Unable to create accepted response");
		}else{
			send_call_reply(response);
			cJSON_Delete(response);
		}
	}
	ESP_LOGI(TAG, "Change availability complete %d->%d", old_is_enabled, storage_Get_IsEnabled());
}

// TODO: ocpp status transition table transition description indicate that pending is used on end of transaction, not entry of new transaction
bool change_availability_if_pending(const uint8_t allowed_new_state){
	if(pending_change_availability && pending_change_availability_state == allowed_new_state){
		change_availability(pending_change_availability_state);
		pending_change_availability = false;
		return true;
	}
	return false;
}

void handle_state_transition(enum ocpp_cp_status_id old_state, enum ocpp_cp_status_id new_state){
	if(old_state == eOCPP_CP_STATUS_FINISHING && new_state != eOCPP_CP_STATUS_FAULTED)
		ocpp_finishing_session = false;

	switch(new_state){
	case eOCPP_CP_STATUS_AVAILABLE:
		ESP_LOGI(TAG, "OCPP STATE AVAILABLE");

		if(change_availability_if_pending(0)) // TODO: check if needed elsewhere
			break;

		switch(old_state){
		case eOCPP_CP_STATUS_PREPARING:
			break;
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
			chargeSession_SetStoppedReason(OCPP_REASON_EV_DISCONNECT);
			stop_transaction();
			SetAuthorized(false);
			break;
		case eOCPP_CP_STATUS_FINISHING:
		case eOCPP_CP_STATUS_RESERVED:
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Available", old_state);
		}
		break;
	case eOCPP_CP_STATUS_PREPARING:
		ESP_LOGI(TAG, "OCPP STATE PREPARING");

		preparing_started = time(NULL);

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_FINISHING:
		case eOCPP_CP_STATUS_RESERVED:
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Preparing", old_state);
		}
		break;
	case eOCPP_CP_STATUS_CHARGING:
		ESP_LOGI(TAG, "OCPP STATE CHARGING");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_PREPARING:
			start_transaction();
			ESP_LOGI(TAG, "Ocpp transactions on file: %d", offlineSession_FindNrOfFiles_ocpp());
			break;
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Charging", old_state);
		}
		break;
	case eOCPP_CP_STATUS_SUSPENDED_EV:
		ESP_LOGI(TAG, "OCPP STATE SUSPENDED_EV");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_PREPARING:
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Suspended EV", old_state);
		}
		break;
	case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		ESP_LOGI(TAG, "OCPP STATE SUSPENDED_EVSE");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_PREPARING:
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Suspended EVSE", old_state);
		}
		break;
	case eOCPP_CP_STATUS_FINISHING:
		ESP_LOGI(TAG, "OCPP STATE FINISHING");

		switch(old_state){
		case eOCPP_CP_STATUS_PREPARING:
			break;
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
			stop_transaction();
			SetAuthorized(false);
			break;
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Finishing", old_state);
		}
		break;
	case eOCPP_CP_STATUS_RESERVED:
		ESP_LOGI(TAG, "OCPP STATE RESERVED");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Reserved", old_state);
		}
		break;
	case eOCPP_CP_STATUS_UNAVAILABLE:
		ESP_LOGI(TAG, "OCPP STATE UNAVAILABLE");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_FINISHING:
		case eOCPP_CP_STATUS_RESERVED:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Suspended Unavailable", old_state);
		}
		break;
	case eOCPP_CP_STATUS_FAULTED:
		ESP_LOGI(TAG, "OCPP STATE FAULTED");

		switch(old_state){
		case eOCPP_CP_STATUS_AVAILABLE:
		case eOCPP_CP_STATUS_PREPARING:
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_FINISHING:
		case eOCPP_CP_STATUS_RESERVED:
		case eOCPP_CP_STATUS_UNAVAILABLE:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Faulted", old_state);
		}
		break;
	}

	if(want_status_notification[old_state-1][new_state-1]){
		ocpp_send_status_notification(new_state, OCPP_CP_ERROR_NO_ERROR, NULL);
	}

	ocpp_old_state = new_state;
}

static bool has_new_id_token(){
	return (NFCGetTagInfo().tagIsValid == true) && (chargeSession_Get().StoppedByRFID == false) && (pending_ocpp_authorize == false);
}

static void handle_available(){
	if(has_new_id_token()) // This will always be false as NFCClearTag() is called by i2cDevices.c if car is disconnected
		authorize(NFCGetTagInfo());
}

static void handle_preparing(){
	/**
	 * From ocpp protocol 1.6 section 3.6:
	 * "Transaction starts at the point that all conditions for charging are met,
	 * for instance, EV is connected to Charge Point and user has been authorized."
	 */
	if((MCU_GetChargeMode() == eCAR_CONNECTED || MCU_GetChargeMode() == eCAR_CHARGING) && isAuthorized){
		ESP_LOGI(TAG, "User actions complete; Attempting to start charging");
		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, MCU_StandAloneCurrent());
		if(ret == MsgWriteAck){
			ESP_LOGI(TAG, "Max Current set");
		}else{
			ESP_LOGE(TAG, "Unable to set max current");
		}

		ret = MCU_SendCommandId(CommandStartCharging);
		if(ret != MsgCommandAck)
		{
			ESP_LOGE(TAG, "Unable to send charging command");
		}
		else{
			ESP_LOGI(TAG, "Charging ok");

			HOLD_SetPhases(1);
			sessionHandler_HoldParametersFromCloud(32.0f, 1);

			//This must be set to stop replying the same SessionIds to cloud
			chargeSession_SetReceivedStartChargingCommand();

			//Clear authorization for next request
			SetAuthorized(false);
		}
	}else if(has_new_id_token()){
		authorize(NFCGetTagInfo());

	}else if(isAuthorized && MCU_GetChargeMode() == eCAR_DISCONNECTED){
		if(preparing_started + storage_Get_ocpp_connection_timeout() < time(NULL)){
			ESP_LOGW(TAG, "Cable was not connected within connection timeout, removind authorization");

			audio_play_nfc_card_denied();
			MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU authorization denied command OK");
			}
			else
			{
				ESP_LOGI(TAG, "MCU authorization denied command FAILED");
			}
			SetAuthorized(false);
			chargeSession_ClearAuthenticationCode();
		}
		else{
			ESP_LOGI(TAG, "Waiting for cable to connect... Timeout: %ld/%d", time(NULL) - preparing_started, storage_Get_ocpp_connection_timeout());
		}
	}
}

static void handle_charging(){
	if(has_new_id_token()){
		authorize_stop(NFCGetTagInfo().idAsString);
		NFCTagInfoClearValid();
	}
}

static void handle_finishing(){
	if(has_new_id_token()){
		authorize(NFCGetTagInfo());
		ocpp_finishing_session = false;
	}
}

static void handle_state(enum ocpp_cp_status_id state){
	switch(state){
	case eOCPP_CP_STATUS_AVAILABLE:
		handle_available();
		break;
	case eOCPP_CP_STATUS_PREPARING:
		handle_preparing();
		break;
	case eOCPP_CP_STATUS_CHARGING:
	case eOCPP_CP_STATUS_SUSPENDED_EV:
	case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		handle_charging();
		break;
	case eOCPP_CP_STATUS_FINISHING:
		handle_finishing();
		break;
	case eOCPP_CP_STATUS_RESERVED:
	case eOCPP_CP_STATUS_UNAVAILABLE:
	case eOCPP_CP_STATUS_FAULTED:
		break;
	}
}

static bool carInterfaceRestartTried = false;
static bool hasSeenCarStateC = false;

///Call this to make carInterface perform a new reset sequence if car is asleep
void sessionHandler_ClearCarInterfaceResetConditions()
{
	carInterfaceRestartTried = false;
	hasSeenCarStateC = false;
}


void sessionHandler_CheckAndSendOfflineSessions()
{
	int nrOfOfflineSessionFiles = offlineSession_FindNrOfFiles();
	int nrOfSentSessions = 0;
	int fileNo;
	for (fileNo = 0; fileNo < nrOfOfflineSessionFiles; fileNo++)
	{
		memset(completedSessionString,0, LOG_STRING_SIZE);

		int fileToUse = offlineSession_FindOldestFile();
		OCMF_CompletedSession_CreateNewMessageFile(fileToUse, completedSessionString);

		//Try sending 3 times. This transmission has been made a blocking call
		int ret = publish_debug_telemetry_observation_CompletedSession(completedSessionString);
		if (ret == 0)
		{
			nrOfSentSessions++;
			/// Sending succeeded -> delete file from flash
			offlineSession_delete_session(fileToUse);
			ESP_LOGW(TAG,"Sent CompletedSession: %i/%i", nrOfSentSessions, nrOfOfflineSessionFiles);
		}
		else
		{
			ESP_LOGE(TAG,"Sending CompletedSession failed! Aborting.");
			break;
		}

		/// Give other tasks time to run if there are many offline sessions to send
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

static bool doCheckOfflineSessions = true;
void sessionHandler_SetOfflineSessionFlag()
{
	doCheckOfflineSessions = true;
}


static uint32_t pulseInterval = PULSE_INIT;
static uint32_t recordedPulseInterval = PULSE_INIT;
static uint16_t pulseSendFailedCounter = 0;
static uint8_t chargeOperatingMode = CHARGE_OPERATION_STATE_UNINITIALIZED;
static bool isOnline = false;
static bool previousIsOnline = true;
static uint32_t pulseCounter = PULSE_INIT_TIME;

enum ChargerOperatingMode sessionHandler_GetCurrentChargeOperatingMode()
{
	return chargeOperatingMode;
}


static void sessionHandler_task()
{
	int8_t rssi = 0;
	wifi_ap_record_t wifidata;
	
	uint32_t onCounter = 0;

	uint32_t onTime = 0;

	///Set high to ensure first pulse sent instantly at start

    uint32_t dataCounter = 0;
    uint32_t dataInterval = 120;

    uint32_t statusCounter = 0;
    uint32_t statusInterval = 15;

    uint32_t LTEsignalInterval = 120;

    uint32_t signalCounter = 0;

    enum CarChargeMode currentCarChargeMode = eCAR_UNINITIALIZED;
    //enum CarChargeMode previousCarChargeMode = eCAR_UNINITIALIZED;

    enum  ChargerOperatingMode previousChargeOperatingMode = CHARGE_OPERATION_STATE_UNINITIALIZED;

    enum CommunicationMode networkInterface = eCONNECTION_NONE;

    uint32_t mcuDebugCounter = 0;
    uint32_t previousDebugCounter = 0;
    uint32_t mcuDebugErrorCount = 0;
    bool sessionIDClearedByCloud = false;

    // Offline parameters
    uint32_t offlineTime = 0;
    uint32_t secondsSinceLastCheck = 10;
    uint32_t resendRequestTimer = 0;
    uint32_t resendRequestTimerLimit = RESEND_REQUEST_TIMER_LIMIT;
    uint8_t nrOfResendRetries = 0;
    uint32_t pingReplyTrigger = 0;

    uint8_t activeWithoutChargingDuration = 0;

	//Used to ensure eMeter alarm source is only read once per occurence
    bool eMeterAlarmBlock = false;

    bool firstTimeAfterBoot = true;
    uint8_t countdown = 5;

    // Prepare for incomming ocpp messages
    attach_call_cb(eOCPP_ACTION_CHANGE_AVAILABILITY_ID, change_availability_cb, NULL);
    attach_call_cb(eOCPP_ACTION_REMOTE_START_TRANSACTION_ID, remote_start_transaction_cb, NULL);
    attach_call_cb(eOCPP_ACTION_REMOTE_STOP_TRANSACTION_ID, remote_stop_transaction_cb, NULL);

    authentication_Init();
    OCMF_Init();
    uint32_t secondsSinceSync = OCMF_INTERVAL_TIME;

    //TickType_t refresh_ticks = pdMS_TO_TICKS(60*1000); //60 minutes
    TickType_t refresh_ticks = pdMS_TO_TICKS(60*60*1000); //60 minutes
    //TickType_t refresh_ticks = pdMS_TO_TICKS(1*60*1000); //1 minutes for testing( also change line in zntp.c for minute sync)
    //TickType_t refresh_ticks = pdMS_TO_TICKS(1*5*1000); //1 minutes for testing( also change line in zntp.c for minute sync)
    signedMeterValues_timer = xTimerCreate( "MeterValueTimer", refresh_ticks, pdTRUE, NULL, on_ocmf_sync_time );

    /// For developement testing only
    //SessionHandler_SetOCMFHighInterval();

    offlineSession_Init();
    ocpp_set_offline_functions(offlineSession_PeekNextMessageTimestamp_ocpp, offlineSession_ReadNextMessage_ocpp,
			    start_transaction_response_cb, error_cb, "start",
			    NULL, error_cb, "stop",
			    NULL, error_cb, "meter");

	while (1)
	{
		if((!setTimerSyncronization))
		{
			//if(zntp_GetTimeAlignementPointDEBUG())
			if(zntp_GetTimeAlignementPoint(OCMFHighInterval))
			{
				ESP_LOGW(TAG, " 1 hour sync!");
				xTimerReset( signedMeterValues_timer, portMAX_DELAY );

				on_ocmf_sync_time(NULL);

				setTimerSyncronization = true;
				secondsSinceSync = 0;
			}
		}

		//The timer must be resynced regularly with the clock to avoid deviation since the clock is updated through NTP.
		secondsSinceSync++;
		secSinceLastOCMFMessage++;

		//Enable resyncronization of timer interrupt with NTP clock on every interval
		//Either sync or timer event will cause trig. If double trig due to clock drifting, only the first one will be completed
		//if((setTimerSyncronization == true) && (secondsSinceSync > 1800))// && MCU_GetchargeMode() != eCAR_CHARGING)	//Try to resync in less than an hour (3400 sec)
		if((setTimerSyncronization == true) && (secondsSinceSync > 15))// && MCU_GetchargeMode() != eCAR_CHARGING)	//Try to resync in less than an hour (3400 sec)
		{
			ESP_LOGW(TAG, " Trig new OCMF timer sync");
			setTimerSyncronization = false;
		}

		onCounter++;


		/// This function is used to test the offline mode as a debug function from Cloud
		offlineHandler_CheckForSimulateOffline();

		isOnline = isMqttConnected();

		if(storage_Get_Standalone() == false)
			offlineHandler_CheckPingReply();

		enum PingReplyState pingReplyState = offlineHandler_GetPingReplyState();

		/// Always ensure offlineCurrentSent is ready to be sent to MCU in case we go offline
		if((isOnline == true) && (pingReplyState != PING_REPLY_OFFLINE))
		{
			offlineHandler_ClearOfflineCurrentSent();
			offlineTime = 0;
		}

		/// Flag as offline if PING_REPLY i in offline mode to reduce transmissions
		if((isOnline == true) && (pingReplyState == PING_REPLY_OFFLINE))
		{
			isOnline = false;
		}

		/// If we are offline and a car has been connected, make sure we
		if((isOnline == false) && (chargeOperatingMode != CHARGE_OPERATION_STATE_DISCONNECTED))
			doCheckOfflineSessions = true;

		/// When we are online without a car connected(No active session file), perform check to see if there are offline sessions to send
		if((isOnline == true) && (doCheckOfflineSessions == true) && (chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			sessionHandler_CheckAndSendOfflineSessions();
			doCheckOfflineSessions = false;

			/// Send diagnostics if there has been any offline sessions
			char sessionString[32] = {0};
			int maxCount = offlineSession_GetMaxSessionCount();
			if(maxCount >= 0)
			{
				snprintf(sessionString, sizeof(sessionString),"MaxOfflineSessions: %i", maxCount);
				ESP_LOGI(TAG, "%s", sessionString);
				publish_debug_telemetry_observation_Diagnostics(sessionString);
			}
		}


		networkInterface = connectivity_GetActivateInterface();

		// Check for MCU communication fault and restart with conditions:
	 	// Not instantly, let mcu upgrade and start
		// Not when OTA in progress
		// Only after given nr of consecutive faults
		int mcuCOMErrors = GetMCUComErrors();
		if((onCounter > 30) && (otaIsRunning() == false) && (mcuCOMErrors > 20))
		{
			ESP_LOGE(TAG, "ESP resetting due to MCUComErrors: %i", mcuCOMErrors);
			publish_debug_message_event("mcuCOMError reset", cloud_event_level_warning);

			storage_Set_And_Save_DiagnosticsLog("#3 MCU COM-error > 20");

			vTaskDelay(5000 / portTICK_PERIOD_MS);

			esp_restart();
		}

		//Check if debugCounter from MCU stops incrementing - reset if persistent
		if((onCounter > 30) && (otaIsRunning() == false))
		{
			previousDebugCounter = mcuDebugCounter;
			mcuDebugCounter = MCU_GetDebugCounter();

			if(mcuDebugCounter == previousDebugCounter)
				mcuDebugErrorCount++;
			else
				mcuDebugErrorCount = 0;

			if(mcuDebugErrorCount == 60)
			{
				ESP_LOGE(TAG, "ESP resetting due to mcuDebugCounter: %i", mcuDebugCounter);
				publish_debug_message_event("mcuDebugCounter reset", cloud_event_level_warning);

				storage_Set_And_Save_DiagnosticsLog("#4 MCU debug counter stopped incrementing");

				vTaskDelay(5000 / portTICK_PERIOD_MS);

				esp_restart();
			}
		}

		if(networkInterface == eCONNECTION_NONE)
		{
			if((onCounter % 10) == 0)
				ESP_LOGI(TAG, "CommunicationMode == eCONNECTION_NONE");

		}


		if(chargeSession_HasNewSessionId() == true)
		{
			int ret = publish_string_observation(SessionIdentifier, chargeSession_GetSessionId());
			if(ret == 0)
				chargeSession_ClearHasNewSession();
		}

		currentCarChargeMode = MCU_GetChargeMode();
		chargeOperatingMode = MCU_GetChargeOperatingMode();


		/// If a car is connected when booting, check for incomplete offline session and resume if incomplete
		if((firstTimeAfterBoot == true) && (chargeOperatingMode != CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			chargeSession_CheckIfLastSessionIncomplete();
			firstTimeAfterBoot = false;
		}

		/// Hold new StartDateTime from requesting Observed time. Cloud expects the same timestamp to be used.
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING) && (previousChargeOperatingMode != CHARGE_OPERATION_STATE_REQUESTING) && (chargeSession_Get().StartDateTime[0] == '\0'))
			InitiateHoldRequestTimeStamp();

		// Handle ocpp state if session type is ocpp and not standalone
		if(storage_Get_session_type_ocpp() && storage_Get_Standalone() == 0){
			enum ocpp_cp_status_id ocpp_new_state = get_ocpp_state(chargeOperatingMode, currentCarChargeMode);

			if(ocpp_new_state != ocpp_old_state){
				handle_state_transition(ocpp_old_state, ocpp_new_state);
			}else{
				handle_state(ocpp_new_state);
			}
		}

		//We need to inform the ChargeSession if a car is connected.
		//If car is disconnected just before a new sessionId is received, the sessionId should be rejected
		if(chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED)
			SetCarConnectedState(false);
		else
			SetCarConnectedState(true);

		//If we are charging when going from offline to online while using zaptec_cloud, send a stop command to change the state to requesting.
		//This will make the Cloud send a new start command with updated current to take us out of offline current mode
		//Check the requestCurrentWhenOnline to ensure we don't send at every token refresh, and only in system mode.
		if((previousIsOnline == false) && (isOnline == true) && offlineHandler_IsRequestingCurrentWhenOnline() && (storage_Get_session_type_ocpp() == false))
		{
			if((chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING) || (chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) || (chargeOperatingMode == CHARGE_OPERATION_STATE_PAUSED))
			{
				ESP_LOGW(TAG, "Got online - Send requesting");

				MessageType ret = MCU_SendCommandId(CommandStopCharging);
				if(ret == MsgCommandAck)
				{

					ESP_LOGI(TAG, "MCU Stop command OK");
				}
				else
				{
					ESP_LOGE(TAG, "MCU Stop command FAILED");
				}

				publish_debug_telemetry_observation_RequestNewStartChargingCommand();
				offlineHandler_SetRequestingCurrentWhenOnline(false);
			}
			else
			{
				ESP_LOGW(TAG, "Got online - Unexpected state requesting");
			}
		}

		/// MQTT connected and pingReply not in offline state
		if(storage_Get_session_type_ocpp() == false){
			if(isOnline && (pingReplyState != PING_REPLY_OFFLINE))
			{
				//Allow disabling send on change in standalone when TransmitInterval is 0
				if(!((storage_Get_TransmitInterval() == 0) && storage_Get_Standalone()))
					publish_telemetry_observation_on_change();
				else
					ESP_LOGE(TAG, "TransmitInterval = 0 in Standalone");

				// If we are in system requesting state, make sure to resend state at increasing interval if it is not changed
				//if((sentOk != 0) && (storage_Get_Standalone() == false) && (chargeOperatingMode == eCONNECTED_REQUESTING))
				if((storage_Get_Standalone() == false) && (chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING))
				{
					resendRequestTimer++;
					ESP_LOGI(TAG, "CHARGE STATE resendTimer: %d/%d", resendRequestTimer, resendRequestTimerLimit);
					if(resendRequestTimer >= resendRequestTimerLimit)
					{
						/// On second request transmission, do the ping-reply to ensure inCharge is responding.
						/// If Cloud does not reply with PingReply command, then go to offline mode.
						pingReplyTrigger++;
						if(pingReplyTrigger == 1)
						{
							update_mqtt_event_pattern(true);
							offlineHandler_UpdatePingReplyState(PING_REPLY_AWAITING_CMD);
						}

						publish_debug_telemetry_observation_ChargingStateParameters();

						if(pingReplyTrigger == 1)
							update_mqtt_event_pattern(false);

						// Reset timer
						resendRequestTimer = 0;

						// Increase timer limit as a backoff routine if Cloud does not answer
						if(resendRequestTimerLimit < 1800)
						{
							resendRequestTimerLimit = RESEND_REQUEST_TIMER_LIMIT << nrOfResendRetries; //pow(2.0, nrOfResendRetries) => 150 300 600 1200 2400
							//ESP_LOGE(TAG, "CHARGE STATE resendRequestTimerLimit: %d nrOfResendRetries %d", resendRequestTimerLimit, nrOfResendRetries);
							nrOfResendRetries++;
						}
					}
				}
				else
				{

					resendRequestTimer = 0;
					resendRequestTimerLimit = RESEND_REQUEST_TIMER_LIMIT;
					nrOfResendRetries = 0;
					pingReplyTrigger = 0;
				}


				offlineTime = 0;
				offlineMode = false;
			}
			else	//Mqtt not connected or PingReply == PING_REPLY_OFFLINE
			{
				if(storage_Get_Standalone() == false)
				{
					offlineTime++;

					/// Use pulseInterval * 2 reported to Cloud to ensure charging is not started until Cloud has flagged the charger as offline
					if(offlineTime > (recordedPulseInterval * 2))
					{
						if(secondsSinceLastCheck < 10)
						{
							secondsSinceLastCheck++;
						}
						if(secondsSinceLastCheck >= 10)
						{
							offlineHandler_CheckForOffline();
							secondsSinceLastCheck = 0;
							offlineMode = true;
						}
					}
					else
					{
						ESP_LOGW(TAG, "System mode: Waiting to declare offline: %d/%d", offlineTime, recordedPulseInterval * 2);
					}
				}
				else
				{
					//OfflineMode is only for System use
					offlineMode = false;
				}
			}
		}

		/*if((previousCarChargeMode == eCAR_UNINITIALIZED) && (currentCarChargeMode == eCAR_DISCONNECTED))
		{
			if(storage_CheckSessionResetFile() > 0)
			{
				esp_err_t clearErr = storage_clearSessionResetInfo();
				ESP_LOGI(TAG, "Clearing csResetSession file due to eCAR_DISCONNECTED at start: %d", clearErr);
			}
			else
			{
				ESP_LOGI(TAG, "No session in csResetSession file");
			}
		}*/

		// Check if car connecting -> start a new session
		if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode <= CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			chargeSession_Start();
		}
		else if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (sessionIDClearedByCloud == true))
		{
			sessionIDClearedByCloud = false;
			chargeSession_Start();
		}
		/*else if((currentCarChargeMode == eCAR_CONNECTED) && (authorizationRequired == true) && (NFCGetTagInfo().tagIsValid == true) && (chargeSession_Get().SessionId[0] == '\0'))
		{
			ESP_LOGW(TAG, "New session due to tag");
			chargeSession_Start();
			//NFCTagInfoClearValid();
		}*/

		bool stoppedByRfid = chargeSession_Get().StoppedByRFID;

		if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (authorizationRequired == true)  && (storage_Get_session_type_ocpp() == false))
		{
			if(isOnline)
			{
				/// Authorized session has cleared by cloud. Send the held autorization to reautorize the connected car.
				if((sessionSession_IsHoldingUserUUID() == true) && (sessionResetMode == eSESSION_RESET_NONE))
				{
					ESP_LOGW(TAG, "Sending and clearing hold-auth: %s", NFCGetTagInfo().idAsString);
					SetPendingRFIDTag(sessionSession_GetHeldUserUUID());
					publish_debug_telemetry_observation_NFC_tag_id(sessionSession_GetHeldUserUUID());
					publish_debug_telemetry_observation_ChargingStateParameters();
					chargeSession_ClearHeldUserUUID();
				}
			}

			if((NFCGetTagInfo().tagIsValid == true) && (stoppedByRfid == false))
			{
				if(isOnline)
				{
					MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
					if(ret == MsgWriteAck)
						ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
					else
						ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

					ESP_LOGW(TAG, "Sending auth: %s", NFCGetTagInfo().idAsString);
					publish_debug_telemetry_observation_NFC_tag_id(NFCGetTagInfo().idAsString);
					publish_debug_telemetry_observation_ChargingStateParameters();
				}

				//System - wait for cloud confirmation before setting RFID-tag
				if((storage_Get_Standalone() == 0) && NFCGetTagInfo().tagIsValid)
				{
					pendingCloudAuthorization = true;
					strcpy(pendingAuthID, NFCGetTagInfo().idAsString);
				}
				//Standalone - set RFID-tag directly
				else
				{
					//Only allow if no tag is set before and tag has been validated
					if((chargeSession_Get().AuthenticationCode[0] == '\0') && (i2cIsAuthenticated() == 1))
					{
						chargeSession_SetAuthenticationCode(NFCGetTagInfo().idAsString);
						//Update session on file with RFID-info
						chargeSession_SaveUpdatedSession();
						//chargeSession_SaveSessionResetInfo();
					}
				}

				NFCTagInfoClearValid();

			}
			//De-authorize in cloud?
			/*else if(stoppedByRfid == true)
			{
				if((strcmp(chargeSession_Get().AuthenticationCode, NFCGetTagInfo().idAsString) == 0))
				{
					if(isOnline)
					{
						publish_debug_telemetry_observation_NFC_tag_id(NULL);
						publish_debug_telemetry_observation_ChargingStateParameters();
					}
					//NFCTagInfoClearValid();
				}
			}*/
		}


		//If the car has not responded to charging being available for 30 seconds, run car interface reset sequence once - like Pro
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) && (currentCarChargeMode != eCAR_CHARGING)  && (carInterfaceRestartTried == false) && (hasSeenCarStateC == false))
		{
			if(activeWithoutChargingDuration <=30)
				activeWithoutChargingDuration++;

			if(activeWithoutChargingDuration == 30)
			{
				MessageType ret = MCU_SendCommandId(MCUCommandRestartCarInterface);
				if(ret == MsgCommandAck)
				{
					ESP_LOGI(TAG, "MCU Restart car OK");
					carInterfaceRestartTried = true;
				}
				else
				{
					ESP_LOGI(TAG, "MCU Restart car FAILED");
				}
			}
		}
		else if(chargeOperatingMode != CHARGE_OPERATION_STATE_CHARGING)
		{
			if(currentCarChargeMode != eCAR_STATE_F)
			{
				carInterfaceRestartTried = false;
				activeWithoutChargingDuration = 0;
			}
		}
		else if((chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) && (currentCarChargeMode == eCAR_CHARGING))
		{
			activeWithoutChargingDuration = 0;
			carInterfaceRestartTried = false;
		}

		//If charging state has occured, do not do carInterface resets
		if(currentCarChargeMode == eCAR_CHARGING)
			hasSeenCarStateC = true;
		else if(currentCarChargeMode == eCAR_DISCONNECTED)
			hasSeenCarStateC = false;

		/// Make sure that a held UserUUID is clear after disconnect (needed when offline)
		if((sessionSession_IsHoldingUserUUID() == true) && (currentCarChargeMode == eCAR_DISCONNECTED))
		{
			chargeSession_ClearHeldUserUUID();
		}

		if(chargeOperatingMode > CHARGE_OPERATION_STATE_REQUESTING)//CHARGE_OPERATION_STATE_DISCONNECTED)
			chargeSession_UpdateEnergy();

		// Check if car connecting -> start a new session
		//if(((chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED)) || (stoppedByRfid == true) || (stoppedByCloud == true))
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			//Do not send a CompletedSession with no SessionId.
			if(chargeSession_Get().SessionId[0] != '\0')
			{
				//Set end time, end energy and OCMF data
				chargeSession_Finalize();
				chargeSession_PrintSession(isOnline, offlineHandler_IsPingReplyOffline());


				/// If it is a systemSession without SessionId from Cloud and no energy, delete it.
				if((storage_Get_Standalone() == 0) && (chargeSession_IsLocalSession() == true))
				{
					if(chargeSession_Get().Energy == 0.0)
					{
						offlineSession_DeleteLastUsedFile();
					}
				}

				if(isOnline)
				{
					sessionHandler_CheckAndSendOfflineSessions();

				}
			}

			//Reset if set
			/*if(stoppedByCloud == true)
			{
				stoppedByCloud = false;
				sessionIDClearedByCloud = true;
				ESP_LOGE(TAG," Session ended by Cloud");

				//char empty[] = "\0";
				publish_string_observation(SessionIdentifier, NULL);
			}*/
			chargeSession_Clear();

			NFCClearTag();

			//Ensure the authentication status is cleared at disconnect
			i2cClearAuthentication();
		}
		

		//If the FinalStopActive bit is set when a car disconnect, make sure to clear the status value used by Cloud
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED) && (GetFinalStopActiveStatus() == true))
		{
			SetFinalStopActiveStatus(0);
		}

		//If session is cleared while car is disconnecting, ensure a new session is not generated incorrectly
		if(chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED)
		{
			sessionIDClearedByCloud = false;
		}


		//Set flag for the periodic OCMF message to show that charging has occured within an
		// interval so that energy message must be sent
		if((chargeOperatingMode != CHARGE_OPERATION_STATE_CHARGING) && (previousChargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING))
		{
			hasCharged = true;
			ESP_LOGW(TAG, " ### No longer charging but must report remaining energy ###");
		}


		//previousCarChargeMode = currentCarChargeMode;
		previousChargeOperatingMode = chargeOperatingMode;

		onTime++;
		dataCounter++;

		if (onTime > 600)
		{
			if ((networkInterface == eCONNECTION_WIFI) || (networkInterface == eCONNECTION_LTE))
			{
				if ((MCU_GetChargeMode() == 12) || (MCU_GetChargeMode() == 9))
					dataInterval = storage_Get_TransmitInterval() * 12;	//When car is disconnected or not charging
				else
					dataInterval = storage_Get_TransmitInterval();	//When car is in charging state

				//LTE SignalQuality internal update interval
				LTEsignalInterval = 7200;
			}
		}

		//Test-mode overrides default
		//if(dataTestInterval != 0)
		//	dataInterval = dataTestInterval;

		if((dataCounter >= dataInterval) && (storage_Get_TransmitInterval() > 0))
		{

			if(isOnline)
			{
				if (networkInterface == eCONNECTION_WIFI)
				{
					if (esp_wifi_sta_get_ap_info(&wifidata)==0)
						rssi = wifidata.rssi;
					else
						rssi = 0;
				}
				else if (networkInterface == eCONNECTION_LTE)
				{
					rssi = GetCellularQuality();
				}

				if(otaIsRunning() == false)
				{
					publish_debug_telemetry_observation_all(rssi);

					if(stackDiagnostics)
						SendStacks();
				}
			}
			else
			{
				ESP_LOGE(TAG, "MQTT not connected");
			}

			dataCounter = 0;
		}


		if (networkInterface == eCONNECTION_LTE)
		{
			signalCounter++;
			if((signalCounter >= LTEsignalInterval) && (otaIsRunning() == false))
			{
				if (isOnline)
				{
					//log_task_info();
					log_cellular_quality(); // check if OTA is in progress before calling this
				}

				signalCounter = 0;
			}
		}

		//pulseCounter++;

		//if(isOnline)
		/*if(connectivity_GetMQTTInitialized())
		{
			if(storage_Get_Standalone() == true)
			{
				pulseInterval = PULSE_STANDALONE;

				/// If other than default on storage, use this to override pulseInterval
				if(storage_Get_PulseInterval() != 60)
					pulseInterval = storage_Get_PulseInterval();
			}
			else if(storage_Get_Standalone() == false)
			{
				if(chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING)
				{
					pulseInterval = PULSE_SYSTEM_CHARGING;
				}
				else
				{
					pulseInterval = PULSE_SYSTEM_NOT_CHARGING;

					/// If other than default on storage, use this to override pulseInterval
					if(storage_Get_PulseInterval() != 60)
						pulseInterval = storage_Get_PulseInterval();
				}
			}


			/// Send new pulse interval to Cloud when it changes with ChargeOperatingMode in System mode.
			/// If charger and cloud does not have the same interval, to much current can be drawn with multiple chargers
			if(pulseInterval != recordedPulseInterval)
			{
				ESP_LOGW(TAG,"Sending pulse interval %d (blocking)", pulseInterval);
				int ret = publish_debug_telemetry_observation_PulseInterval(pulseInterval);

				if(ret == ESP_OK)
				{
					recordedPulseInterval = pulseInterval;
					ESP_LOGW(TAG,"Registered pulse interval");
					pulseSendFailedCounter = 0;
				}
				else
				{
					ESP_LOGE(TAG,"Pulse interval send failed");

					//If sending fails, don't continue sending forever -> timeout and set anyway
					pulseSendFailedCounter++;
					if(pulseSendFailedCounter == 90)
					{
						recordedPulseInterval = pulseInterval;
						pulseSendFailedCounter = 0;
					}
				}
			}



			/// If going from offline to online - ensure new pulse is sent instantly
			/// Cloud sets charger as online within one minute after new pulse is received.
			if((isOnline == true) && (previousIsOnline == false))
				pulseCounter = PULSE_INIT_TIME;

			if(pulseCounter >= pulseInterval)
			{
				ESP_LOGW(TAG, "PULSE");
				publish_cloud_pulse();

				pulseCounter = 0;
			}
		}*/


		statusCounter++;
		if(statusCounter >= statusInterval)
		{

			if (networkInterface == eCONNECTION_LTE)
			{
				ESP_LOGI(TAG,"LTE: %d %%  DataInterval: %d  Pulse: %d/%d", GetCellularQuality(), dataInterval, pulseCounter, pulseInterval);
			}
			else if (networkInterface == eCONNECTION_WIFI)
			{
				if (esp_wifi_sta_get_ap_info(&wifidata)==0)
					rssi = wifidata.rssi;
				else
					rssi = 0;

				ESP_LOGI(TAG,"WIFI: %d dBm  DataInterval: %d  Pulse: %d/%d", rssi, dataInterval, pulseCounter, pulseInterval);
			}

			//This is to make cloud settings visible during developement
			if(storage_Get_Standalone() == false)
			{
				sessionHandler_PrintParametersFromCloud();
				if((MCU_GetChargeMode() == 12))
				{
					//Clear if car is disconnected
					currentSetFromCloud = 0.0;
					phasesSetFromCloud = 0;
				}
			}

			chargeSession_PrintSession(isOnline, offlineHandler_IsPingReplyOffline());

			statusCounter = 0;
		}


		if (isOnline)
		{
			if (startupSent == false)
			{
				if((networkInterface == eCONNECTION_WIFI))
				{
					if (esp_wifi_sta_get_ap_info(&wifidata)==0)
						rssi = wifidata.rssi;
					else
						rssi = 0;

					publish_debug_telemetry_observation_WifiParameters();
				}
				if (networkInterface == eCONNECTION_LTE)
				{
					//log_task_info();
					if(otaIsRunning() == false)
						rssi = log_cellular_quality();

					publish_debug_telemetry_observation_LteParameters();
				}

				publish_debug_telemetry_observation_StartUpParameters();
				publish_debug_telemetry_observation_all(rssi);
				publish_debug_telemetry_observation_local_settings();
				publish_debug_telemetry_observation_power();


				/// If we start up after an unexpected reset. Send and clear the diagnosticsLog.
				if(storage_Get_DiagnosticsLogLength() > 0)
				{
					publish_debug_telemetry_observation_DiagnosticsLog();
					storage_Clear_And_Save_DiagnosticsLog();
				}

				//Since they are synced on start they no longer need to be sent at every startup. Can even cause inconsistency.
				//publish_debug_telemetry_observation_cloud_settings();

				startupSent = true;
			}


			if(CloudSettingsAreUpdated() == true)
			{
				if(GetNewInstallationIdFlag() == true)
				{
					update_installationId();
					ClearNewInstallationIdFlag();
				}

				int published = publish_debug_telemetry_observation_cloud_settings();
				if (published == 0)
				{
					ClearCloudSettingsAreUpdated();
					ESP_LOGW(TAG,"Cloud settings flag cleared");
				}
				else
				{
					ESP_LOGE(TAG,"Cloud settings flag NOT cleared");
				}
			}


			if(LocalSettingsAreUpdated() == true)
			{
				//Give some time to ensure all values are set
				vTaskDelay(pdMS_TO_TICKS(1000));

				int published = publish_debug_telemetry_observation_local_settings();
				if (published == 0)
				{
					ClearLocalSettingsAreUpdated();
					ESP_LOGW(TAG,"Local settings flag cleared");
				}
				else
				{
					ESP_LOGE(TAG,"Local settings flag NOT cleared");
				}
			}


			if(RFIDListIsUpdated() >= 0)
			{
				//Give some time to ensure it is sendt after cloud has received command-ack
				vTaskDelay(pdMS_TO_TICKS(1000));

				int published = publish_uint32_observation(AuthenticationListVersion, (uint32_t)RFIDListIsUpdated());
				if (published == 0)
				{
					ClearRfidListIsUpdated();
					ESP_LOGW(TAG,"RFID version value cleared");
				}
				else
				{
					ESP_LOGE(TAG,"RFID version value NOT cleared");
				}
			}

			if(GetReportGridTestResults() == true)
			{
				//Give some time to ensure all values are set
				vTaskDelay(pdMS_TO_TICKS(3000));

				ZapMessage rxMsg = MCU_ReadParameter(GridTestResult);
				if(rxMsg.length > 0)
				{
					char * gtr = (char *)calloc(rxMsg.length+1, 1);
					memcpy(gtr, rxMsg.data, rxMsg.length);
					int published = publish_debug_telemetry_observation_GridTestResults(gtr);
					free(gtr);

					if (published == 0)
					{
						ClearReportGridTestResults();
						ESP_LOGW(TAG,"GridTest flag cleared");
					}
					else
					{
						ESP_LOGE(TAG,"GridTest flag NOT cleared");
					}
				}
				else
				{
					ESP_LOGW(TAG,"GridTest length = 0");
					ClearReportGridTestResults();
				}
			}


			if(GetMCUDiagnosticsResults() == true)
			{
				ZapMessage rxMsg = MCU_ReadParameter(ParamDiagnosticsString);
				if(rxMsg.length > 0)
				{
					char * gtr = (char *)calloc(rxMsg.length+1, 1);
					memcpy(gtr, rxMsg.data, rxMsg.length);

					int published = -1;

					if(currentCarChargeMode == eCAR_CHARGING)
					{
						countdown = 5;
					}
					else
					{
						if(countdown > 0)
							countdown--;
					}

					if(countdown > 0)
						published = publish_debug_telemetry_observation_Diagnostics(gtr);

					free(gtr);

					if (published == 0)
					{
						//ClearMCUDiagnosicsResults();
						//ESP_LOGW(TAG,"Diagnostics flag cleared");
						ESP_LOGW(TAG,"Diagnostics sent");
					}
					else
					{
						ESP_LOGE(TAG,"Diagnostics not sent");
					}
				}
				else
				{
					//ESP_LOGW(TAG,"Diagnostics length = 0");
					//ClearMCUDiagnosicsResults();
				}

			}
			else
			{
				countdown = 5;
			}

			if(GetESPDiagnosticsResults() == true)
			{
				char * rfidBuffer = storage_GetRFIDbuffer();
				publish_debug_telemetry_observation_Diagnostics(rfidBuffer);
				storage_FreeRFIDbuffer();

				ClearESPDiagnosicsResults();
			}


			if(GetInstallationConfigOnFile() == true)
			{
				publish_debug_telemetry_observation_InstallationConfigOnFile();
				ClearInstallationConfigOnFile();
			}

			if(MCU_ServoCheckRunning() == true)
			{
				///Wait while the servo test is performed
				vTaskDelay(pdMS_TO_TICKS(4000));
				char payload[128];
				uint16_t servoCheckStartPosition = MCU_GetServoCheckParameter(ServoCheckStartPosition);
				uint16_t servoCheckStartCurrent = MCU_GetServoCheckParameter(ServoCheckStartCurrent);
				uint16_t servoCheckStopPosition = MCU_GetServoCheckParameter(ServoCheckStopPosition);
				uint16_t servoCheckStopCurrent = MCU_GetServoCheckParameter(ServoCheckStopCurrent);

				snprintf(payload, sizeof(payload), "ServoCheck: %i, %i, %i, %i Range: %i", servoCheckStartPosition, servoCheckStartCurrent, servoCheckStopPosition, servoCheckStopCurrent, (servoCheckStartPosition-servoCheckStopPosition));
				ESP_LOGI(TAG, "ServoCheckParams: %s", payload);
				publish_debug_telemetry_observation_Diagnostics(payload);

				MCU_ServoCheckClear();
			}


			if(HasNewData() == true)
			{
				int published = publish_diagnostics_observation(GetATBuffer());

				if (published == 0)
				{
					ClearATBuffer();
				}
			}

			if(logCurrents == true)
			{
				if(logCurrentsCounter < 300)
					logCurrentsCounter++;
				if(logCurrentsCounter == 300)
					logCurrents = false;

				if(logCurrentsCounter % 2 == 0)
					publish_debug_telemetry_observation_power();
			}


			if(MCU_GetWarnings() & 0x1000000) /// WARNING_EMETER_ALARM
			{
				if(eMeterAlarmBlock == false)
				{
					/// Delay to ensure alarm source is updated on MCU
					vTaskDelay(pdMS_TO_TICKS(1000));

					ZapMessage rxMsg = MCU_ReadParameter(ParamEmeterAlarm);
					if((rxMsg.length == 2) && (rxMsg.identifier == ParamEmeterAlarm))
					{
						char buf[50] = {0};
						snprintf(buf, sizeof(buf), "eMeterAlarmSource: 0x%02X%02X", rxMsg.data[0], rxMsg.data[1]);
						publish_debug_message_event(buf, cloud_event_level_warning);
					}
				}

				eMeterAlarmBlock = true;
			}
			else
			{
				eMeterAlarmBlock = false;
			}
			


			if(onTime % 15 == 0)//15
			{
				struct MqttDataDiagnostics mqttDiag = MqttGetDiagnostics();
				char buf[150]={0};
				snprintf(buf, sizeof(buf), "%d MQTT data: Rx: %d %d #%d - Tx: %d %d #%d - Tot: %d (%d)", onTime, mqttDiag.mqttRxBytes, mqttDiag.mqttRxBytesIncMeta, mqttDiag.nrOfRxMessages, mqttDiag.mqttTxBytes, mqttDiag.mqttTxBytesIncMeta, mqttDiag.nrOfTxMessages, (mqttDiag.mqttRxBytesIncMeta + mqttDiag.mqttTxBytesIncMeta), (int)((1.1455 * (mqttDiag.mqttRxBytesIncMeta + mqttDiag.mqttTxBytesIncMeta)) + 4052.1));//=1.1455*C11+4052.1
				ESP_LOGI(TAG, "**** %s ****", buf);

				if(onTime % 7200 == 0)
				{
					//Only publish if activated by command
					if(GetDatalog())
						publish_debug_telemetry_observation_Diagnostics(buf);

					MqttDataReset();
					ESP_LOGW(TAG, "**** Hourly MQTT data reset ****");
				}
			}


			/*if(CloudCommandCurrentUpdated() == true)
			{
				MessageType rxMsg = MCU_ReadFloatParameter(ParamChargeCurrentUserMax);
				float currentSetToMCU = GetFloat(rxMsg.data);

				//Give some time to ensure all values are set

				int published = publish_debug_telemetry_observation_local_settings();
				if (published == 0)
				{
					ClearCloudCommandCurrentUpdated();
					ESP_LOGW(TAG,"Command feedback flag cleared");
				}
				else
				{
					ESP_LOGE(TAG,"Command feedback flag NOT cleared");
				}
			}*/


			//publish_telemetry_observation_on_change();

		}


		/// Indicate offline with led LED
		if((storage_Get_Standalone() == false) && ((networkInterface == eCONNECTION_WIFI) || (networkInterface == eCONNECTION_LTE)))
		{
			if((offlineTime % 30 == 0) && (isOnline == false))
			{
				MessageType ret = MCU_SendCommandId(CommandIndicateOffline);
				if(ret == MsgCommandAck)
					ESP_LOGI(TAG, "MCU LED offline pulse OK. ");
				else
					ESP_LOGE(TAG, "MCU LED offline pulse FAILED");
			}
		}


		/*if((useTransitionState == true) && (chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			SetTransitionOperatingModeState(false);
			useTransitionState = false;
			ESP_LOGE(TAG, "Transition state END");
		}*/

		previousIsOnline = isOnline;


		if(sessionResetMode != eSESSION_RESET_NONE)
		{
			sessionHandler_StopAndResetChargeSession();
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}



void sessionHandler_InitiateResetChargeSession()
{
	sessionResetMode = eSESSION_RESET_INITIATED;
	ESP_LOGW(TAG, "ResetSession initiated");
}

void sessionHandler_StopAndResetChargeSession()
{
	///First send STOP command
	if(sessionResetMode == eSESSION_RESET_INITIATED)
	{
		MessageType ret = MCU_SendCommandId(CommandStopCharging);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "MCU Reset-Stop command OK");

			sessionResetMode = eSESSION_RESET_STOP_SENT;
		}
		else
		{
			ESP_LOGE(TAG, "MCU ResetSession command FAILED");
			sessionResetMode= eSESSION_RESET_NONE;
		}
	}

	///When charging has stopped(9) -Finalize session
	else if(sessionResetMode == eSESSION_RESET_STOP_SENT)
	{
		if(MCU_GetChargeMode() != eCAR_CHARGING)
		{
			sessionResetMode = eSESSION_RESET_FINALIZE;
		}
	}


	else if(sessionResetMode == eSESSION_RESET_FINALIZE)
	{
		ESP_LOGI(TAG, "Transition state START");
		SetTransitionOperatingModeState(true);
		sessionResetMode = eSESSION_RESET_DO_RESET;
	}

	else if((sessionResetMode == eSESSION_RESET_DO_RESET) && (chargeSession_HasSessionId() == false))
	{

		MessageType ret = MCU_SendCommandId(CommandResetSession);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "MCU ResetSession command OK");
			SetTransitionOperatingModeState(false);
			sessionResetMode = eSESSION_RESET_NONE;
			ESP_LOGI(TAG, "Transition state STOP");
			//return 200;
		}
		else
		{
			ESP_LOGE(TAG, "MCU ResetSession command FAILED");
			//return 400;
		}
	}
}



/*
 * If we have received an already set SessionId from Cloud while in CHARGE_OPERATION_STATE_CHARGING
 * This indicates that cloud does not have the correct chargeOperatingMode recorded.
*/
void ChargeModeUpdateToCloudNeeded()
{
	if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_CHARGING && storage_Get_session_type_ocpp() == false)
		publish_debug_telemetry_observation_ChargingStateParameters();
}

void StackDiagnostics(bool state)
{
	stackDiagnostics = state;
}


/*
 * Call this function to resend startup parameters
 */
void ClearStartupSent()
{
	startupSent = false;
}


static TaskHandle_t taskSessionHandleOCMF = NULL;
int sessionHandler_GetStackWatermarkOCMF()
{
	if(taskSessionHandleOCMF != NULL)
		return uxTaskGetStackHighWaterMark(taskSessionHandleOCMF);
	else
		return -1;
}


static TaskHandle_t taskSessionHandle = NULL;
int sessionHandler_GetStackWatermark()
{
	if(taskSessionHandle != NULL)
		return uxTaskGetStackHighWaterMark(taskSessionHandle);
	else
		return -1;
}


void sessionHandler_Pulse()
{
	pulseCounter++;

	if(connectivity_GetMQTTInitialized())
	{
		if(storage_Get_Standalone() == true)
		{
			pulseInterval = PULSE_STANDALONE;

			/// If other than default on storage, use this to override pulseInterval
			if(storage_Get_PulseInterval() != 60)
				pulseInterval = storage_Get_PulseInterval();
		}
		else if(storage_Get_Standalone() == false)
		{
			if(chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING)
			{
				pulseInterval = PULSE_SYSTEM_CHARGING;
			}
			else
			{
				pulseInterval = PULSE_SYSTEM_NOT_CHARGING;

				/// If other than default on storage, use this to override pulseInterval
				if(storage_Get_PulseInterval() != 60)
					pulseInterval = storage_Get_PulseInterval();
			}
		}


		/// Send new pulse interval to Cloud when it changes with ChargeOperatingMode in System mode.
		/// If charger and cloud does not have the same interval, to much current can be drawn with multiple chargers
		if(pulseInterval != recordedPulseInterval && storage_Get_session_type_ocpp() == false)
		{
			ESP_LOGI(TAG,"Sending pulse interval %d (blocking)", pulseInterval);
			int ret = publish_debug_telemetry_observation_PulseInterval(pulseInterval);

			if(ret == ESP_OK)
			{
				recordedPulseInterval = pulseInterval;
				ESP_LOGI(TAG,"Registered pulse interval");
				pulseSendFailedCounter = 0;
			}
			else
			{
				ESP_LOGE(TAG,"Pulse interval send failed");

				//If sending fails, don't continue sending forever -> timeout and set anyway
				pulseSendFailedCounter++;
				if(pulseSendFailedCounter == 90)
				{
					recordedPulseInterval = pulseInterval;
					pulseSendFailedCounter = 0;
				}
			}
		}



		/// If going from offline to online - ensure new pulse is sent instantly
		/// Cloud sets charger as online within one minute after new pulse is received.
		if((isOnline == true) && (previousIsOnline == false))
			pulseCounter = PULSE_INIT_TIME;

		if(pulseCounter >= pulseInterval)
		{
			ESP_LOGI(TAG, "PULSE");
			publish_cloud_pulse();

			pulseCounter = 0;
		}
	}
}


void sessionHandler_TestOfflineSessions(int nrOfSessions, int nrOfSignedValues)
{
	MqttSetSimulatedOffline(true);

	int i, j;
	for(i = 0; i < nrOfSessions; i++)
	{
		ESP_LOGI(TAG, "Generating OfflineSession nr: %i", i);
		chargeSession_Start();

		for(j = 0; j < nrOfSignedValues; j++)
		{
			OCMF_CompletedSession_AddElementToOCMFLog('T', i, j*1.0);
		}

		chargeSession_SetEnergyForTesting(j*1.0);

		chargeSession_Finalize();

		chargeSession_Clear();
	}

	MqttSetSimulatedOffline(false);
}

void sessionHandler_init(){

	ocmf_sync_semaphore = xSemaphoreCreateBinary();
	xTaskCreate(ocmf_sync_task, "ocmf", 5000, NULL, 3, &taskSessionHandleOCMF);

	completedSessionString = malloc(LOG_STRING_SIZE);
	//Got stack overflow on 5000, try with 6000
	xTaskCreate(sessionHandler_task, "sessionHandler_task", 6000, NULL, 3, &taskSessionHandle);

}
