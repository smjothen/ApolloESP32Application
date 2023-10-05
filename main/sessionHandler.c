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
#include "zaptec_protocol_warnings.h"
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
#include "../components/calibration/include/calibration.h"
#include "offline_log.h"
#include "offlineSession.h"
#include "offlineHandler.h"
#include "../components/audioBuzzer/audioBuzzer.h"
#include "fat.h"
#include "chargeController.h"

#include "ocpp_task.h"
#include "ocpp_transaction.h"
#include "ocpp_smart_charging.h"
#include "ocpp_auth.h"
#include "ocpp.h"
#include "ocpp_json/ocppj_validation.h"
#include "messages/call_messages/ocpp_call_cb.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_reason.h"
#include "types/ocpp_authorization_status.h"
#include "types/ocpp_authorization_data.h"
#include "types/ocpp_id_tag_info.h"
#include "types/ocpp_availability_type.h"
#include "types/ocpp_availability_status.h"
#include "types/ocpp_charge_point_status.h"
#include "types/ocpp_remote_start_stop_status.h"
#include "types/ocpp_charge_point_error_code.h"
#include "types/ocpp_ci_string_type.h"
#include "types/ocpp_date_time.h"
#include "types/ocpp_reservation_status.h"
#include "types/ocpp_cancel_reservation_status.h"
#include "types/ocpp_charging_profile_status.h"
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
		ESP_LOGW(TAG, "****** DOUBLE OCMF %" PRId32 " -> RETURNING ******", secSinceLastOCMFMessage);
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
	}else if(offline_log_attempt_send()==0){
		ESP_LOGI(TAG, "energy log empty");
		state_log_empty = true;
	}

	if ((state_charging && state_log_empty) || (hasRemainingEnergy && state_log_empty)){
		publish_result = publish_string_observation_blocked(
			SignedMeterValue, OCMPMessage, 10000
		);

		if(publish_result<0){
			offline_log_append_energy(timeSec, energy);
		}

	}else if(state_charging || hasRemainingEnergy){
		ESP_LOGI(TAG, "failed to empty log, appending new measure");
		offline_log_append_energy(timeSec, energy);
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

void SetPendingRFIDTag(const char * pendingTag)
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
		"[MEMORY USE] (GetFreeHeapSize now: %" PRIu32 ", GetMinimumEverFreeHeapSize: %" PRIu32 ", heap_caps_get_free_size: %d)",
		xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize(), free_heap_size
	);
	ESP_LOGD(TAG, "%s", formated_memory_use);

	// heap_caps_print_heap_info(MALLOC_CAP_EXEC|MALLOC_CAP_32BIT|MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL|MALLOC_CAP_DEFAULT|MALLOC_CAP_IRAM_8BIT);
	heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

	publish_diagnostics_observation(formated_memory_use);
	ESP_LOGD(TAG, "log_task_info done");
}




static bool startupSent = false;
static bool setTimerSyncronization = false;
static bool reportChargingStateCommandSent = false;

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


static uint32_t previousNotification = 0;

static bool rcdBit0 = false;
static bool previousRcdBit0 = false;
static uint32_t errorCountAB = 0;
static uint32_t errorCountABCTotal = 0;

static bool rcdBit1 = false;
static bool previousRcdBit1 = false;
void NotificationHandler()
{
	uint32_t combinedNotification = GetCombinedNotifications();

	if(combinedNotification != previousNotification)
	{
		ESP_LOGW(TAG, "Notification change:  %" PRIu32 " -> %" PRIu32, previousNotification, combinedNotification);
	}
	previousNotification = combinedNotification;

	/// Handle RCD notification in state A and B
	previousRcdBit0 = rcdBit0;
	rcdBit0 = (combinedNotification & 0x1);

	if((rcdBit0 == true) && (previousRcdBit0 == false))
	{
		errorCountAB++;
		errorCountABCTotal++;
		ESP_LOGW(TAG, "RCD error in state A/B: %" PRIu32, errorCountAB);

		/// Send event on different level to be able to search event messages based on severity
		if(errorCountAB == 1)
		{
			publish_debug_message_event("RCD error A/B trig", cloud_event_level_warning);
		}
		else if(errorCountAB == 10)
		{
			publish_debug_message_event("RCD error A/B 10", cloud_event_level_warning);
		}
		else if(errorCountAB == 100)
		{
			publish_debug_message_event("RCD error A/B 100", cloud_event_level_warning);
		}
		else if(errorCountAB == 1000)
		{
			publish_debug_message_event("RCD error A/B 1000", cloud_event_level_warning);
		}
	}


	/// Handle RCD notification in state C
	previousRcdBit1 = rcdBit1;
	rcdBit1 = (combinedNotification & 0x2);

	if(rcdBit1 != previousRcdBit1)
	{
		errorCountABCTotal++;

		//Check for RCD error indication
		if(rcdBit1)
		{
			ZapMessage rxMsg = MCU_ReadParameter(RCDErrorCount);
			uint8_t readErrorCount = 0;
			if((rxMsg.length == 1) && (rxMsg.identifier == RCDErrorCount))
				readErrorCount = rxMsg.data[0];


			char noteBuf[25];
			snprintf(noteBuf, 25, "RCD error count: %i", readErrorCount);
			publish_debug_telemetry_observation_Diagnostics(noteBuf);
			ESP_LOGW(TAG, "%s", noteBuf);

			if(readErrorCount <= 4)
				publish_debug_message_event("RCD error trig", cloud_event_level_warning);
			else if(readErrorCount == 5)
				publish_debug_message_event("RCD error trig warning", cloud_event_level_error);
		}
		else
		{
			///Ensure car is waked up if delay is so long it goes into sleep mode
			sessionHandler_ClearCarInterfaceResetConditions();
		}
	}

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
static uint16_t logCurrentsInterval = 5;
static uint16_t logCurrentStop = 0;
/*
 * Function for sending power values at configurable interval for testing
 */
void SessionHandler_SetLogCurrents(int interval)
{
	logCurrentsInterval = interval;

	//Stop logging after 5 min if interval is 60 sec or less
	if(logCurrentsInterval <= 60)
		logCurrentStop = 300;
	else
		logCurrentStop = 0;

	if(interval > 0)
	{
		logCurrents = true;

	}
	else
	{
		logCurrents = false;
	}

	ESP_LOGW(TAG, "Logging: %i, %i", logCurrents, logCurrentsInterval);

	logCurrentsCounter = 0;
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
 * The current implementation of the ocpp component only allows valid messages to be created. Transaction related messages that do not
 * have a valid transaction id must be stored.
 *
 * What should happen if id is not recieved is specifid in ocpp 1.6 errata sheet v4.0:
 * "the Charge Point SHALL send any Transaction related messages for this transaction to the Central System with a transactionId = -1.
 * The Central System SHALL respond as if these messages refer to a valid transactionId, so that the Charge Point is not blocked by this."
 */
int * transaction_id = NULL;
bool transaction_id_is_valid = false;

time_t transaction_start = 0;
int meter_start = 0;
bool pending_change_availability = false;
bool ocpp_finishing_session = false; // Used to differentiate between eOCPP_CP_STATUS_FINISHING and eOCPP_CP_STATUS_PREPARING
bool ocpp_faulted = false; // Used to differentiate between faulted state and any other state with same features
ocpp_id_token ocpp_start_token = {0}; // Charge session may be cleared after ocpp token has been presented and before StartTransaction has been created. Charge session should be updated with this
enum ocpp_cp_status_id ocpp_faulted_exit_state = eOCPP_CP_STATUS_UNAVAILABLE; // ocpp only allows transitioning from faulted to pre-faulted state
bool pending_change_availability_state;
time_t preparing_started = 0;

struct ocpp_reservation_info {
	int connector_id;
	time_t expiry_date;
	char id_tag[21];
	char parent_id_tag[21];
	int reservation_id;
	bool is_reservation_state;
};

struct ocpp_reservation_info * reservation_info = NULL;

bool sessionHandler_OcppTransactionIsActive(uint connector_id){
	if(connector_id == 1 || connector_id == 0){
		return (transaction_start != 0);
	}else{
		return false;
	}
}

int * sessionHandler_OcppGetTransactionId(uint connector_id, bool * valid_out){
	if(sessionHandler_OcppTransactionIsActive(connector_id)){
		*valid_out = transaction_id_is_valid;
		return transaction_id;
	}else{
		*valid_out = false;
		return NULL;
	}
}

time_t sessionHandler_OcppTransactionStartTime(){
	return transaction_start;
}

float ocpp_min_limit = -1.0f;
float ocpp_max_limit = -1.0f;
uint8_t ocpp_active_phases = 0;
uint8_t ocpp_requested_phases = 0;

void sessionHandler_OcppSetChargingVariables(float min_charging_limit, float max_charging_limit, uint8_t number_phases){
	ESP_LOGI(TAG, "Got new charging variables: minimum: %f -> %f, maximum: %f -> %f, phases %d -> %d",
		ocpp_min_limit, min_charging_limit, ocpp_max_limit, max_charging_limit, ocpp_requested_phases, number_phases);

	if(ocpp_min_limit == -1){
		ESP_LOGI(TAG, "Initializing ocpp charging values");
		ocpp_min_limit = storage_Get_CurrentInMinimum();
		ocpp_max_limit = storage_Get_StandaloneCurrent();
		ocpp_active_phases = storage_Get_StandalonePhase();
		ocpp_requested_phases = ocpp_active_phases;
	}

	if(ocpp_min_limit != min_charging_limit){
 		ESP_LOGI(TAG, "Changing minimum current: %f -> %f", ocpp_min_limit, min_charging_limit);
		MessageType ret = MCU_SendFloatParameter(ParamCurrentInMinimum, min_charging_limit);
		if(ret == MsgWriteAck){
			ESP_LOGI(TAG, "Minimum current updated");
			ocpp_min_limit = min_charging_limit;
		}else{
			ESP_LOGE(TAG, "Unable to update minimum current");
		}
	}

	if(ocpp_max_limit != max_charging_limit){
		ESP_LOGI(TAG, "Changing maximum current: %f -> %f", ocpp_max_limit, max_charging_limit);
		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, max_charging_limit);
		if(ret == MsgWriteAck){
			ESP_LOGI(TAG, "Max current updated");
			ocpp_max_limit = max_charging_limit;
		}else{
			ESP_LOGE(TAG, "Unable to update max current");
		}

		if(max_charging_limit >= 0.0f && max_charging_limit <= 0.05f){
			ESP_LOGI(TAG, "OCPP charging variable set to 0. Attempting to pausing charging");

			MessageType ret = MCU_SendCommandId(CommandStopChargingFinal);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU CommandStopChargingFinal command OK during set charging variables");
				SetFinalStopActiveStatus(1);
			}
			else
			{
				ESP_LOGE(TAG, "MCU CommandStopChargingFinal command FAILED during ocpp set charging variables");
			}

		}else if(ocpp_max_limit >= 0.0f && ocpp_max_limit <= 0.05f && max_charging_limit > 0.05f){
			ESP_LOGI(TAG, "OCPP charging variable no longer set to 0. Attempting to resume charging");

			MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK during ocpp set charging variables");
				SetFinalStopActiveStatus(0);
			}
			else
			{
				ESP_LOGE(TAG, "MCU CommandResumeChargingMCU command FAILED during ocpp set charging variables");
			}

			ret = MCU_SendCommandId(CommandStartCharging);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU CommandStartCharging OK during ocpp set charging variables");
			}
			else
			{
				ESP_LOGE(TAG, "MCU CommandStartCharging FAILED during ocpp set charging variables");
			}
		}
	}

	ocpp_requested_phases = number_phases;

#ifdef OCPP_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED
	if(ocpp_requested_phases != ocpp_active_phases && !sessionHandler_OcppTransactionIsActive(1)){

		ESP_LOGW(TAG, "OCPP requested a legal change of number of phases, but this is currently not supported or meaningfull in current context");

		ocpp_active_phases = ocpp_requested_phases;
	}
#endif
}

void resume_if_allowed(){

	if(ocpp_max_limit >= 0.0f && ocpp_max_limit <= 0.05f) // Suspended evse
		return;

	ESP_LOGI(TAG, "Allowed to resume charging");

	MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK during resume check");
		SetFinalStopActiveStatus(0);
	}
	else
	{
		ESP_LOGE(TAG, "MCU CommandResumeChargingMCU command FAILED during resume check");
	}

	ret = MCU_SendCommandId(CommandStartCharging);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU CommandStartCharging OK during resume check");
	}
	else
	{
		ESP_LOGE(TAG, "MCU CommandStartCharging FAILED during resume check");
	}
}

void sessionHandler_OcppTransitionToFaulted(){
	ocpp_faulted = true;
	ocpp_faulted_exit_state = ocpp_old_state;
}

void sessionHandler_OcppTransitionFromFaulted(){
	ocpp_faulted = false;
}

/*
 * ocpp describes preparing as:
 * 'When a Connector becomes no longer available for a new user but there is no ongoing Transaction (yet). Typically a Connector
 * is in preparing state when a user presents a tag, inserts a cable or a vehicle occupies the parking bay (Operative)'
 *
 * ocpp also defines 'ConnectionTimeOut' as:
 * "Interval *from beginning of status: 'Preparing' until incipient Transaction is automatically canceled, due to failure of EV driver to
 * (correctly) insert the charging cable connector(s) into the appropriate socket(s). The Charge Point SHALL go back to the original
 * state, probably: 'Available'."
 *
 * Note that the ConnectionTimeOut only describes timeout for connecting cable and not for presenting idTag.
 * We assume 'Original' in the description refers to the state prior to preparing
 *
 * Transition B1 refers to ConnectionTimeOut:
 * 'Intended usage is ended (e.g. plug removed, bay no longer occupied, second presentation of idTag, time out (configured by the configuration
 * key: ConnectionTimeOut) on expected user action)'
 *
 * Transition B6 also refers to a timeout, but is not a cable connection timeout:
 * 'Timed out. Usage was initiated (e.g. insert plug, bay occupancy detection), but idTag not presented within timeout.'
 *
 * There are no other timeout configuration for preparing. If it is intended to use the ConnectionTimeOut for transition B6 then the 'Original state'
 * transition requirement (SHALL) would be broken if prior state was the 'Available' described as probable by the ConnectionTimeOut definition.
 * Transition back to Available instead would not be productive as the car is still connected and the CP would therefore not be available to other users.
 * The transition table also defines transitions to preparing that can not be reversed. It is allowed to enter preparing from reserved, but not allowed to
 * enter reserved from preparing.
 *
 * The approach taken is:
 * 1. Reserved surpass preparing and available.
 * 2. Reservations end when user is authorized (instead of new transaction), transitioning to faulted/unavailable or timed out.
 * 2. Only timeout for preparing is when authorized and waiting for cable to connect.
 * 3. Only result when connection times out is available.
 *
 * This approach should be acceptable as the CP does not detect parking bay occupancy and there are only two requirements for charging: authorization and cable connected.
 * Only transition to preparing where ConnectionTimeOut is relevant is therefore from available.
 * All other transitions from preparing are not considered timeouts.
 */
void transition_to_preparing(){
	preparing_started = time(NULL);
}

void sessionHandler_OcppStopTransaction(enum ocpp_reason_id reason){
	ESP_LOGI(TAG, "Stopping charging");

	SetAuthorized(false);
	sessionHandler_InitiateResetChargeSession();
	chargeSession_SetStoppedReason(reason);
}

static void start_transaction_response_cb(const char * unique_id, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received start transaction response");

	bool is_current_transaction = false;
	bool valid = false;
	struct ocpp_transaction_start_stop_cb_data * data = cb_data;

	char err_str[64];

	if(data != NULL){
		if(transaction_id != NULL){
			ESP_LOGI(TAG, "transaction id is: %d, transaction entry is %d", *transaction_id, data->transaction_entry);

			int received_id;
			enum ocppj_err_t err = ocppj_get_int_field(payload, "transactionId", true, &received_id, err_str, sizeof(err_str));
			if(err == eOCPPJ_NO_ERROR){
				if(*transaction_id == data->transaction_entry){
					ESP_LOGI(TAG, "Sat valid transaction_id for current transaction");

					*transaction_id = received_id;
					transaction_id_is_valid = true;
					is_current_transaction = true;
					ocpp_set_active_transaction_id(transaction_id);
				}

				if(ocpp_transaction_set_real_id(data->transaction_entry, received_id) == ESP_OK){
					ESP_LOGI(TAG, "Sat valid transaction_id for stored transaction");
				}else{
					ESP_LOGE(TAG, "Failed to set valid id for stored transaction");
				}

				// Only set the session id if cloud did not yet and response is related to active transaction
				if(strstr(chargeSession_GetSessionId(), "-") == NULL && is_current_transaction){
					char transaction_id_str[37]; // when not using ocpp directly, session id is UUID

					snprintf(transaction_id_str, sizeof(transaction_id_str), "%d", *transaction_id);
					chargeSession_SetSessionIdFromCloud(transaction_id_str);
				}
			}else{
				ESP_LOGE(TAG, "Set id for transaction: [%s] '%s'", ocppj_error_code_from_id(err), err_str);
			}
		}

		if(cJSON_HasObjectItem(payload, "idTagInfo")){

			struct ocpp_id_tag_info id_tag_info = {0};
			if(id_tag_info_from_json(cJSON_GetObjectItem(payload, "idTagInfo"), &id_tag_info, err_str, sizeof(err_str))
				!= eOCPPJ_NO_ERROR){
				ESP_LOGW(TAG, "Received idTagInfo in startTransaction.conf is invalid: %s", err_str);
			}else{
				if(data->id_tag[0] != '\0'){
					ocpp_on_id_tag_info_recieved(data->id_tag, &id_tag_info);
				}else{
					ESP_LOGE(TAG, "StartTransaction callback data contains no idToken, but StartTransaction.conf contains idTagInfo");
				}

				enum ocpp_authorization_status_id status_id = ocpp_get_status_from_id_tag_info(&id_tag_info);
				ESP_LOGI(TAG, "Central system returned status %s", ocpp_authorization_status_from_id(status_id));
				valid = (status_id == eOCPP_AUTHORIZATION_STATUS_ACCEPTED);
			}

			if(id_tag_info.parent_id_tag != NULL){
				ESP_LOGI(TAG, "Adding parent id to current charge session");
				chargeSession_SetParentId(id_tag_info.parent_id_tag);
			}
		}else{
			ESP_LOGE(TAG, "Recieved start transaction response lacking 'idTagInfo'");
			valid = false;
		}

		chargeSession_SetAuthenticationCode(data->id_tag);

	}else if(data == NULL){
		ESP_LOGE(TAG, "Callback data is missing for start transaction response");
	}


	if(!valid){

		if(is_current_transaction){
			ESP_LOGW(TAG, "Current transaction deauthorized");
			SetAuthorized(false);

			if(storage_Get_ocpp_stop_transaction_on_invalid_id()){
				ESP_LOGW(TAG, "Attempting to stop transaction");
				sessionHandler_InitiateResetChargeSession();
				chargeSession_SetStoppedReason(eOCPP_REASON_DE_AUTHORIZED);
			}else{
				ESP_LOGW(TAG, "Attempting to pause transaction");
				MessageType ret = MCU_SendCommandId(CommandStopChargingFinal);
				if(ret == MsgCommandAck)
				{
					ESP_LOGI(TAG, "MCU CommandStopChargingFinal command OK during deautorize");
					SetFinalStopActiveStatus(1);
				}
				else
				{
					ESP_LOGE(TAG, "MCU CommandStopChargingFinal command FAILED during deautorize");
				}
			}
		}else{
			ESP_LOGE(TAG, "An inactive transaction was deauthorized");
		}
	}
}

static void stop_transaction_response_cb(const char * unique_id, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Stop transaction response success");

	struct ocpp_transaction_start_stop_cb_data * data = cb_data;

	if(data != NULL && data->id_tag[0] != '\0' && cJSON_HasObjectItem(payload, "idTagInfo")){
		char err_str[64];
		struct ocpp_id_tag_info id_tag_info;

		if(id_tag_info_from_json(cJSON_GetObjectItem(payload, "idTagInfo"), &id_tag_info, err_str, sizeof(err_str)) != eOCPPJ_NO_ERROR){
			ESP_LOGE(TAG, "Received idTagInfo in stop transaction is invalid: %s", err_str);
		}else{
			ocpp_on_id_tag_info_recieved(data->id_tag, &id_tag_info);
		}
	}
}

bool pending_ocpp_authorize = false;

static void start_transaction_error_cb(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data){
	ESP_LOGE(TAG, "Failed start trasnsaction request");

	struct ocpp_transaction_start_stop_cb_data * data = cb_data;

	if(data != NULL){
		if(transaction_id != NULL && *transaction_id == data->transaction_entry){
			ESP_LOGE(TAG, "Setting -1 as valid id for active transaction");
			*transaction_id = -1;
			transaction_id_is_valid = true;
		}
	}else{
		ESP_LOGE(TAG, "Missing cb_data for failed start transaction");
	}


	error_logger(unique_id, error_code, error_description, error_details, NULL);
}

static struct ocpp_meter_value_list * current_meter_values = NULL;
static struct ocpp_meter_value_list * intermittent_meter_value = NULL;

static size_t current_meter_values_length = 0;

/*
 * This function is used to save transaction related meter values that are meant to be used in StopTransaction.req.
 * Ocpp errata v4.0 specify a configuration key to limit how many meter values may be in a StopTransaction.req. Too
 * prevent crashes due to no more memory, this key is set by CONFIG_OCPP_STOP_TRANSACTION_MAX_METER_VALUES.
 */
void sessionHandler_OcppTransferMeterValues(uint connector_id, struct ocpp_meter_value_list * values, size_t length){
	if(connector_id != 1 || sessionHandler_OcppTransactionIsActive(connector_id) == false){
		ESP_LOGE(TAG, "sessionHandler got notified of meter values without ongoing transaction, value recieved too late and transactionData might be wrong");
		goto error;
	}

	if(current_meter_values == NULL){

		current_meter_values = ocpp_create_meter_list();
		if(current_meter_values == NULL){
			ESP_LOGE(TAG, "Unable to create meter value list to store transaction data");
			goto error;
		}

		current_meter_values_length = 0;
		intermittent_meter_value = NULL;
	}


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

	while(current_meter_values_length > CONFIG_OCPP_STOP_TRANSACTION_MAX_METER_VALUES){
		ESP_LOGW(TAG, "Too stop transaction meter values exceed maximum (%d > %d), deleting intermittent values", current_meter_values_length, CONFIG_OCPP_STOP_TRANSACTION_MAX_METER_VALUES);

		/*
		 * Deletion starts with 2nd item, continues with 4th then 6th and so on.
		 * It will attempt to not delete the first or last item if CONFIG_OCPP_STOP_TRANSACTION_MAX_METER_VALUES > 1
		 */
		if(intermittent_meter_value == NULL // If no values have been deleted yet
			|| intermittent_meter_value->next == NULL || intermittent_meter_value->next->next == NULL){ // or last item deleted was at the end of list

			intermittent_meter_value = current_meter_values; // Start deleting from the start of meter values

		} else { // At least one deletion has occured and not at end of list
			intermittent_meter_value = intermittent_meter_value->next; // Skip odd numbered value
		}

		if(intermittent_meter_value->next == NULL){ // Should only happen if compiled with max values set to 0; should delete entire list
			ocpp_meter_list_delete(current_meter_values); // Delete the entire list as it is only 1 value
			current_meter_values = NULL;
			intermittent_meter_value = NULL;

		}else{ // should delete one item
			struct ocpp_meter_value_list * tmp = intermittent_meter_value->next;
			intermittent_meter_value->next = tmp->next;

			if(tmp->value != NULL && tmp->value->sampled_value != NULL)
				ocpp_sampled_list_delete(tmp->value->sampled_value);

			free(tmp->value);
			free(tmp);
		}

		current_meter_values_length--;
	}

	ESP_LOGI(TAG, "Current meter values length: %d (MAX: %d)", current_meter_values_length, CONFIG_OCPP_STOP_TRANSACTION_MAX_METER_VALUES);
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
			transaction_id, transaction_id_is_valid, &connector, 1, false);
}

static void start_sample_interval(enum ocpp_reading_context_id context){
	ESP_LOGI(TAG, "Starting sample interval");

	sample_handle = xTimerCreate("Ocpp sample",
				pdMS_TO_TICKS(storage_Get_ocpp_meter_value_sample_interval() * 1000),
				pdTRUE, NULL, sample_meter_values);

	if(sample_handle == NULL){
		ESP_LOGE(TAG, "Unable to create sample handle");
	}else{
		if(xTimerStart(sample_handle, pdMS_TO_TICKS(200)) != pdPASS){
			ESP_LOGE(TAG, "Unable to start sample interval");
		}else{
			init_interval_measurands(context);
			ESP_LOGI(TAG, "Started sample interval");
		}
	}
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
			transaction_id, transaction_id_is_valid, &connector, 1, false);
}

void stop_transaction(enum ocpp_cp_status_id ocpp_state){
	stop_sample_interval();
	ocpp_set_transaction_is_active(false, 0);

	int meter_stop = floor(get_accumulated_energy() * 1000);

	time_t timestamp = time(NULL);

	enum ocpp_reason_id reason = chargeSession_Get().StoppedReason;

	if(reason == eOCPP_REASON_OTHER){ // No reason has been set, try to detect more specific reason
		ESP_LOGW(TAG, "OCPP reason not explicitly set");

		/*
		 * We use a low counter to detect reboot, as it is possible that ocpp connected quickly and transaction
		 * was very short.
		 */
		if(MCU_GetDebugCounter() < 10){
			ESP_LOGW(TAG, "Resent reboot caused transaction to end");
			if(MCU_GetResetSource() & 1<<2){ // brownout
				reason = eOCPP_REASON_POWER_LOSS;
			}else{
				reason = eOCPP_REASON_REBOOT;
			}

		}else if(ocpp_state == eOCPP_CP_STATUS_AVAILABLE){
			ESP_LOGI(TAG, "Looks like transaction stopped due to car disconnect");
			reason = eOCPP_REASON_EV_DISCONNECT;
		}
	}

	if(reason == eOCPP_REASON_EV_DISCONNECT && storage_Get_ocpp_stop_transaction_on_ev_side_disconnect()){

		/*
		 * Ocpp 1.6 specify that a status notification with state finishing, no error, and info about ev disconnect.
		 * SHOULD be sendt to the CS.
		 * The motivation for this is unclear as the same information is given in the stop transaction and the
		 * specification also allows transition from charging/suspended to available (not just finishing).
		 * Finishing seems to be intended for situation where user action is required before new transaction or new user, yet
		 * we must inform CS of finishing before entering available.
		 */
		ocpp_send_status_notification(eOCPP_CP_STATUS_FINISHING, OCPP_CP_ERROR_NO_ERROR, "EV side disconnected",
					NULL, NULL, true, false);
	}

	transaction_id_is_valid = false;

	if(ocpp_transaction_enqueue_stop(chargeSession_Get().StoppedByRFID ? chargeSession_Get().StoppedById : NULL,
						meter_stop, timestamp, reason, current_meter_values) != 0){

		ESP_LOGE(TAG, "Failed to enqueue stop transaction");
	}

	ocpp_meter_list_delete(current_meter_values);
	current_meter_values = NULL;

	free(transaction_id);
	transaction_start = 0;

	ocpp_set_active_transaction_id(NULL);
	transaction_id = NULL;
}

void start_transaction(){
	transaction_start = time(NULL);

	int meter_start = floor(get_accumulated_energy() * 1000);

	MessageType ret = MCU_SendUint8Parameter(PermanentCableLock, !storage_Get_ocpp_unlock_connector_on_ev_side_disconnect());
	if(ret != MsgWriteAck){
		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Unable to apply UnlockConnectorOnEVSideDisconnect",
					NULL, NULL, true, false);
		ESP_LOGE(TAG, "Unable to set UnlockConnectorOnEVSideDisconnect on MCU");
	}

	if(chargeSession_Get().AuthenticationCode[0] == '\0'){
		if(ocpp_start_token[0] != '\0'){
			chargeSession_SetAuthenticationCode(ocpp_start_token);
		}else{
			ESP_LOGW(TAG, "No id tag for charge session. Using default value");
			chargeSession_SetAuthenticationCode(storage_Get_ocpp_default_id_token());
		}
	}

	transaction_id = malloc(sizeof(int));
	if(transaction_id == NULL){
		ESP_LOGE(TAG, "Unable to create buffer for transaction id");
		return;
	}

	if(ocpp_transaction_enqueue_start(1, chargeSession_Get().AuthenticationCode, meter_start,
						(reservation_info != NULL) ? &reservation_info->reservation_id : NULL,
						time(NULL), transaction_id) != 0){

		ESP_LOGE(TAG, "Unable to enqueue start transaction request");
	}

	ESP_LOGI(TAG, "Transaction id set to: %d before StartTransaction.conf", *transaction_id);

	ocpp_set_transaction_is_active(true, transaction_start);

	free(reservation_info);
	reservation_info = NULL;

	if(storage_Get_ocpp_meter_value_sample_interval() > 0){

		start_sample_interval(eOCPP_CONTEXT_TRANSACTION_BEGIN);

		uint connector = 1;
		handle_meter_value(eOCPP_CONTEXT_TRANSACTION_BEGIN,
				storage_Get_ocpp_meter_values_sampled_data(), storage_Get_ocpp_stop_txn_sampled_data(),
				transaction_id, transaction_id_is_valid, &connector, 1, false);

	}
}

const char * id_token_from_tag(const char * tag_id){
	if(tag_id != NULL && strncmp(tag_id, "nfc-", sizeof("nfc-")-1) == 0){
		return tag_id + (sizeof("nfc-")-1);
	}else{
		return tag_id;
	}
}

void authorize(struct TagInfo tag, void (*on_accept)(const char *), void (*on_deny)(const char *)){
	pending_ocpp_authorize = true;

	MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
	if(ret == MsgWriteAck)
		ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
	else
		ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

	ocpp_authorize(id_token_from_tag(tag.idAsString), on_accept, on_deny);
}

static enum  SessionResetMode sessionResetMode = eSESSION_RESET_NONE;

void start_charging_on_tag_accept(const char * tag){
	ESP_LOGI(TAG, "Start transaction accepted for %s", tag);
	pending_ocpp_authorize = false;

	audio_play_nfc_card_accepted();
	MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
	if(ret != MsgCommandAck)
	{
		ESP_LOGE(TAG, "Unable to grant authorization to MCU");
	}
	else{
		ESP_LOGI(TAG, "Authorization granted ok");

		if(tag != NULL && tag[0] != '\0'){
			SetPendingRFIDTag(tag);
		}else{
			SetPendingRFIDTag(storage_Get_ocpp_default_id_token());
		}

		strncpy(ocpp_start_token, pendingAuthID, sizeof(ocpp_id_token));
		SetAuthorized(true);
	}
}

void start_charging_on_tag_deny(const char * tag){
	ESP_LOGW(TAG, "Start transaction denied for %s", tag);
	pending_ocpp_authorize = false;

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

	ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_NOT_AUTHORIZED);
	if(ret == MsgWriteAck)
		ESP_LOGI(TAG, "Ack on SESSION_NOT_AUTHORIZED");
	else
		ESP_LOGW(TAG, "NACK on SESSION_NOT_AUTHORIZED!!!");

}

void cancel_authorization_on_tag_accept(const char * tag_1, const char * tag_2){
	ESP_LOGI(TAG, "Cancel authorization for preparing token: %s comparable to %s", tag_1, tag_2);
	pending_ocpp_authorize = false;

	audio_play_nfc_card_accepted();
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
	sessionHandler_InitiateResetChargeSession();
	chargeSession_ClearAuthenticationCode();
}

void cancel_authorization_on_tag_deny(const char * tag_1, const char * tag_2){
	ESP_LOGI(TAG, "Won't cancel authorization for preparing token: %s not comparable to %s", tag_1, tag_2);
	pending_ocpp_authorize = false;

	audio_play_nfc_card_denied();
}

void stop_charging_on_tag_accept(const char * tag_1, const char * tag_2){
	ESP_LOGI(TAG, "Stop transaction accepted for %s on charge session (id: %d) made by '%s'",
		tag_1, (transaction_id != NULL) ? *transaction_id : -1, tag_2);

	ESP_LOGI(TAG, "Authorized to stop transaction");
	SetAuthorized(false);

	audio_play_nfc_card_accepted();

	chargeSession_SetStoppedByRFID(true, tag_1);
	sessionHandler_InitiateResetChargeSession();
}

void stop_charging_on_tag_deny(const char * tag_1, const char * tag_2){

	ESP_LOGW(TAG, "Stop transaction denied for %s on charge session (id: %d) made by '%s'",
		tag_1, (transaction_id != NULL) ? *transaction_id : -1, tag_2);

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

void reserved_on_tag_accept(const char * tag_1, const char * tag_2){
	ESP_LOGI(TAG, "Reservation accepted for '%s' on reservation (id: %d) made by '%s'",
		tag_1, reservation_info->reservation_id, tag_2);

	reservation_info->is_reservation_state = false;
	start_charging_on_tag_accept(tag_1);

	//If car is connected then CommandAuthorizationGranted will start charging without a
	// CommandStartCharging. Therefore accepting a tag while reserved may transition from
	// reserved to charging. This is not allowed in ocpp without transitioning to preparing
	// first. We fake this transition.
	ocpp_old_state = eOCPP_CP_STATUS_PREPARING;
	transition_to_preparing();

	ocpp_send_status_notification(eOCPP_CP_STATUS_PREPARING, OCPP_CP_ERROR_NO_ERROR, NULL, NULL, NULL, false, false);

}

void reserved_on_tag_deny(const char * tag_1, const char * tag_2){
	ESP_LOGW(TAG, "Reservation denied for '%s' on reservation (id: %d) made by '%s'",
		tag_1, reservation_info->reservation_id, tag_2);

	start_charging_on_tag_deny(tag_1);
}

enum ocpp_cp_status_id saved_state = eOCPP_CP_STATUS_UNAVAILABLE;
void sessionHandler_OcppSaveState(){
	saved_state = ocpp_old_state;
}

bool sessionHandler_OcppStateHasChanged(){
	return saved_state != ocpp_old_state;
}

void sessionHandler_OcppSendState(bool is_trigger){
	ocpp_send_status_notification(ocpp_old_state, OCPP_CP_ERROR_NO_ERROR, NULL, NULL, NULL, true, is_trigger);
}

static int change_availability(bool new_available){

	bool old_available = storage_Get_availability_ocpp();

	if(old_available == new_available)
		return 0;

	storage_Set_availability_ocpp(new_available);
	ESP_LOGI(TAG, "Set availability to %s", new_available ? "operative" : "inoperative");

	if(new_available && storage_Get_IsEnabled() == 0){
		ocpp_send_status_notification(ocpp_old_state, OCPP_CP_ERROR_OTHER_ERROR, "'Available' ineffective, CP disabled in portal", NULL, NULL,  true, false);

	}else{

		uint8_t mcu_new_enabled = (new_available && storage_Get_IsEnabled() == 1) ? 1 : 0;

		MessageType ret = MCU_SendUint8Parameter(ParamIsEnabled, mcu_new_enabled);
		if(ret == MsgWriteAck)
		{
			ESP_LOGW(TAG, "MCU IsEnabled changed successfully to %d", mcu_new_enabled);
		}
		else
		{
			ESP_LOGE(TAG, "Unable to change MCU IsEnabled to %d, reverting availability change", mcu_new_enabled);
			storage_Set_availability_ocpp(old_available);
			return -1;
		}
	}

	storage_SaveConfiguration();
	return 0;
}

time_t boot_timestamp = 0;
bool ocpp_startup = true;

// See transition table in section on Status notification in ocpp 1.6 specification
static enum ocpp_cp_status_id get_ocpp_state(){

	if(ocpp_faulted){
		return eOCPP_CP_STATUS_FAULTED;
	}else if(ocpp_old_state == eOCPP_CP_STATUS_FAULTED){
		return ocpp_faulted_exit_state;
	}

	if(sessionResetMode != eSESSION_RESET_NONE)
		return eOCPP_CP_STATUS_FINISHING;

	// The state returned by MCU does not by itself indicate if it isEnabled/operable, so we check storage first.
	// We also require the charger to be 'Accepted by central system' (optional) see 4.2.1. of the ocpp 1.6 specification
	if(storage_Get_IsEnabled() == 0 || !storage_Get_availability_ocpp() || get_registration_status() != eOCPP_REGISTRATION_ACCEPTED){
		return eOCPP_CP_STATUS_UNAVAILABLE;
	}

	enum CarChargeMode charge_mode = MCU_GetChargeMode();
	enum ChargerOperatingMode operating_mode = MCU_GetChargeOperatingMode();

	switch(operating_mode){
	case CHARGE_OPERATION_STATE_UNINITIALIZED:
		return eOCPP_CP_STATUS_UNAVAILABLE;

	case CHARGE_OPERATION_STATE_DISCONNECTED:

		if(reservation_info != NULL && reservation_info->is_reservation_state && !isAuthorized){
			return eOCPP_CP_STATUS_RESERVED;

		}else if(isAuthorized || pending_ocpp_authorize){
			if(ocpp_old_state == eOCPP_CP_STATUS_CHARGING
				|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EV
				|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EVSE){

				return eOCPP_CP_STATUS_FINISHING;
			}else{
				return eOCPP_CP_STATUS_PREPARING;
			}

		}else{
			return eOCPP_CP_STATUS_AVAILABLE;
		}

	case CHARGE_OPERATION_STATE_REQUESTING:
		if(reservation_info != NULL && reservation_info->is_reservation_state && !isAuthorized){
			return eOCPP_CP_STATUS_RESERVED;

		}else if(isAuthorized && sessionHandler_OcppTransactionIsActive(1)){
			return eOCPP_CP_STATUS_SUSPENDED_EVSE;

		}else if(ocpp_finishing_session // not transitioning away from FINISHED
			|| ocpp_old_state == eOCPP_CP_STATUS_CHARGING // transition C6
			|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EV // transition D6
			|| ocpp_old_state == eOCPP_CP_STATUS_SUSPENDED_EVSE){ // transition E6

		        return eOCPP_CP_STATUS_FINISHING;
		}else{ // Else it must be transition A2, F2, G2, H2 or not transitioning away from perparing
			return eOCPP_CP_STATUS_PREPARING;
		}

	case CHARGE_OPERATION_STATE_ACTIVE:
		if(reservation_info != NULL && reservation_info->is_reservation_state && !isAuthorized){
			return eOCPP_CP_STATUS_RESERVED;

		}else{
			return eOCPP_CP_STATUS_AVAILABLE;
		}

	case CHARGE_OPERATION_STATE_CHARGING:
		if(charge_mode == eCAR_CONNECTED) // Car reports connected but not charging
			return eOCPP_CP_STATUS_SUSPENDED_EV;

		return eOCPP_CP_STATUS_CHARGING;

	case CHARGE_OPERATION_STATE_STOPPING:
		return eOCPP_CP_STATUS_FINISHING;

	case CHARGE_OPERATION_STATE_PAUSED:
		if(charge_mode == eCAR_CHARGING || GetFinalStopActiveStatus() == 1){

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

static void reserve_now_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to reserve now");

	int connector_id = 0;
	time_t expiry_date = 0;
	char * id_tag = NULL;
	char * id_parent = NULL;
	int reservation_id = 0;

	bool err = false;
	cJSON * ocpp_error;

	if(cJSON_HasObjectItem(payload, "connectorId")){
		cJSON * connector_id_json = cJSON_GetObjectItem(payload, "connectorId");
		if(cJSON_IsNumber(connector_id_json)){
			connector_id = connector_id_json->valueint;

			if(connector_id < 0 || connector_id > CONFIG_OCPP_NUMBER_OF_CONNECTORS){
				err = true;
				ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "'connectorId' does not name a valid connector", NULL);
			}
		}else{
			err = true;
			ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'connectorId' to be integer type", NULL);
		}
	}else{
		err = true;
		ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'connectorId' field", NULL);
	}

	if(!err && cJSON_HasObjectItem(payload, "expiryDate")){
		cJSON * expiry_date_json = cJSON_GetObjectItem(payload, "expiryDate");
		if(cJSON_IsString(expiry_date_json)){
			expiry_date = ocpp_parse_date_time(expiry_date_json->valuestring);

			if(expiry_date == 0 || expiry_date == (time_t)-1){
				err = true;
				ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'expiryDate' to be a valid dateTime type", NULL);
			}else if(expiry_date < time(NULL)){
				err = true;
				ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "Expected 'expiryDate' to be a time in the future", NULL);
			}
		}else{
			err = true;
			ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'expiryDate' to be a valid dateTime type", NULL);
		}
	}else{
		err = true;
		ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'expiryDate' field", NULL);
	}

	if(!err && cJSON_HasObjectItem(payload, "idTag")){
		cJSON * id_tag_json = cJSON_GetObjectItem(payload, "idTag");
		if(cJSON_IsString(id_tag_json)){
			id_tag = id_tag_json->valuestring;

			if(!is_ci_string_type(id_tag, 20)){
				err = true;
				ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'idTag' to be idToken type (CiString20Type)", NULL);
			}
		}else{
			err = true;
			ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'idTag' to be idToken type", NULL);
		}
	}else{
		err = true;
		ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'idTag' field", NULL);
	}

	if(!err && cJSON_HasObjectItem(payload, "parentIdTag")){
		cJSON * id_parent_json = cJSON_GetObjectItem(payload, "parentIdTag");
		if(cJSON_IsString(id_parent_json)){
			id_parent = id_parent_json->valuestring;

			if(!is_ci_string_type(id_parent, 20)){
				err = true;
				ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'parentIdTag' to be idToken type (CiString20Type)", NULL);
			}
		}else{
			err = true;
			ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'parentIdTag' to be idToken type", NULL);
		}
	}

	if(!err && cJSON_HasObjectItem(payload, "reservationId")){
		cJSON * reservation_id_json = cJSON_GetObjectItem(payload, "reservationId");
		if(cJSON_IsNumber(reservation_id_json)){
			reservation_id = reservation_id_json->valueint;
		}else{
			err = true;
			ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'reservationId' to be integer type", NULL);
		}
	}else{
		err = true;
		ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'reservationId' field", NULL);
	}

	if(err){
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Error occured during parsing of ReserveNow.req, but no error was created");
		}else{
			send_call_reply(ocpp_error);
		}

		return;
	}

	cJSON * reply = NULL;

#ifndef CONFIG_OCPP_RESERVE_CONNECTOR_ZERO_SUPPORTED
	if(connector_id == 0){
		ESP_LOGW(TAG, "Reservation request was for connector 0 which is not supported by configuration");

		reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_REJECTED);
		send_call_reply(reply);
		return;
	}
#endif
	enum ocpp_cp_status_id state = get_ocpp_state();

	if(storage_Get_AuthenticationRequired()){
		switch(state){
		case eOCPP_CP_STATUS_AVAILABLE:
			ESP_LOGI(TAG, "Available, accepting reservation request");

			if(reservation_info == NULL)
				reservation_info = malloc(sizeof(struct ocpp_reservation_info));

			if(reservation_info == NULL){
				ESP_LOGE(TAG, "Unable to allocate space for reservation id");
				reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Unable to allocate memory for reservation", NULL);

			}else{
				reservation_info->connector_id = connector_id;
				reservation_info->expiry_date = expiry_date;
				strcpy(reservation_info->id_tag, id_tag);
				if(id_parent != NULL){
					strcpy(reservation_info->parent_id_tag, id_parent);
				}else{
					reservation_info->parent_id_tag[0] = '\0';
				}
				reservation_info->reservation_id = reservation_id;
				reservation_info->is_reservation_state = true;

				ESP_LOGI(TAG, "Connector %d reserved by '%s'. Set to expire in %" PRId64 " seconds", connector_id, id_tag, expiry_date - time(NULL));
				reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_ACCEPTED);
			}
			break;

		case eOCPP_CP_STATUS_PREPARING:
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_FINISHING:
		case eOCPP_CP_STATUS_RESERVED:
			ESP_LOGI(TAG, "Occupied, denied reservation request");
			reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_OCCUPIED);
			break;

		case eOCPP_CP_STATUS_UNAVAILABLE:
			ESP_LOGI(TAG, "Unavailable, denied reservation request");
			reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_UNAVAILABLE);
			break;

		case eOCPP_CP_STATUS_FAULTED:
			ESP_LOGI(TAG, "Faulted, denied reservation request");
			reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_FAULTED);
			break;

		default:
			ESP_LOGE(TAG, "Unhandled state during reservation");
			return;
		}
	}else{
		reply = ocpp_create_reserve_now_confirmation(unique_id, OCPP_RESERVATION_STATUS_REJECTED);
	}

	send_call_reply(reply);
}

static void cancel_reservation_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Recieved request to cancel reservation");

	if(cJSON_HasObjectItem(payload, "reservationId")){
		cJSON * reservation_id_json = cJSON_GetObjectItem(payload, "reservationId");
		if(cJSON_IsNumber(reservation_id_json)){
			cJSON * response;

			if(reservation_info != NULL && reservation_id_json->valueint == reservation_info->reservation_id){
				ESP_LOGI(TAG, "Reservation with id %d cancelation accepted", reservation_info->reservation_id);
				free(reservation_info);
				reservation_info = NULL;
				response = ocpp_create_cancel_reservation_confirmation(unique_id, OCPP_CANCEL_RESERVATION_STATUS_ACCEPTED);
			}else{
				ESP_LOGW(TAG, "Rejected attempt to cancel reservation. Requested id %d", reservation_id_json->valueint);
				response = ocpp_create_cancel_reservation_confirmation(unique_id, OCPP_CANCEL_RESERVATION_STATUS_REJECTED);
			}

			if(response == NULL){
				ESP_LOGE(TAG, "Unable to create reservation response");
			}else{
				send_call_reply(response);
			}
		}else{
			cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'connectorId' to be integer and 'type' to be AvailabilityType", NULL);
			if(ocpp_error == NULL){
				ESP_LOGE(TAG, "Unable to create call error for type constraint violation");
			}else{
				send_call_reply(ocpp_error);
			}
			return;
		}
	}else{
		cJSON * ocpp_error = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'connectorId' and 'type' fields", NULL);
		if(ocpp_error == NULL){
			ESP_LOGE(TAG, "Unable to create call error for formation violation");
		}else{
			send_call_reply(ocpp_error);
		}
		return;
	}
}

static void remote_start_transaction_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to remote start transaction");
	char err_str[128] = {0};

	int connector_id;
	enum ocppj_err_t err = ocppj_get_int_field(payload, "connectorId", false, &connector_id, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_VALUE){

		if(err != eOCPPJ_NO_ERROR){
			goto error;
		}else{
			if(connector_id <= 0 || connector_id > CONFIG_OCPP_NUMBER_OF_CONNECTORS){
				err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
				strcpy(err_str, "Expected connectorId to name a valid connector");
				goto error;
			}
		}
	}

	char * id_tag;
	err = ocppj_get_string_field(payload, "idTag", true, &id_tag, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_ERROR)
		goto error;

	bool accept_request;

	enum ocpp_cp_status_id state = get_ocpp_state();
	//TODO: Check if intended behaviour when reserved is to accept tag and only validate against reservation when AuthorizeRemoteTxRequests
	if(state == eOCPP_CP_STATUS_AVAILABLE || state == eOCPP_CP_STATUS_PREPARING || state == eOCPP_CP_STATUS_FINISHING || state == eOCPP_CP_STATUS_RESERVED){
		accept_request = true;

	}else{
		accept_request = false;
	}

	if(accept_request && cJSON_HasObjectItem(payload, "chargingProfile")){

		struct ocpp_charging_profile * charging_profile = calloc(sizeof(struct ocpp_charging_profile), 1);
		if(charging_profile == NULL){
			err = eOCPPJ_ERROR_INTERNAL;
			strcpy(err_str, "Unable to allocate memory for charging profile");
			goto error;
		}

		err = ocpp_charging_profile_from_json(cJSON_GetObjectItem(payload, "chargingProfile"),
						CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL,
						ocpp_get_allowed_charging_rate_units(),
						CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS,
						charging_profile, err_str, sizeof(err_str));
		if(err != eOCPPJ_NO_ERROR){
			free(charging_profile);
			goto error;
		}

		if(charging_profile->profile_purpose != eOCPP_CHARGING_PROFILE_PURPOSE_TX || charging_profile->transaction_id != NULL){
			ocpp_free_charging_profile(charging_profile);

			err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
			strcpy(err_str, "chargingProfile is valid, but RemoteStarttransaction requires purpose to be txProfile and no transactionId");
			goto error;
		}

		if(update_charging_profile(charging_profile) != ESP_OK){
			err = eOCPPJ_ERROR_INTERNAL;
			strcpy(err_str, "Unable to set chargingProfile");
			goto error;
		}
	}

	cJSON * response;
	if(accept_request){
		response = ocpp_create_remote_start_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_ACCEPTED);
	}else{
		response = ocpp_create_remote_start_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_REJECTED);
	}

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create remote start transaction response");
	}else{
		send_call_reply(response);
	}

	if(!accept_request)
		return;

	if(storage_Get_ocpp_authorize_remote_tx_requests()){
		struct TagInfo tag ={
			.tagIsValid = true,
		};
		strcpy(tag.idAsString, id_tag);

		if(state == eOCPP_CP_STATUS_RESERVED){
			ocpp_authorize_compare(id_tag, NULL,
					reservation_info->id_tag, reservation_info->parent_id_tag,
					reserved_on_tag_accept, reserved_on_tag_deny);
		}else{
			ocpp_finishing_session = false; // Only relevant if state is Finishing
			authorize(tag, start_charging_on_tag_accept, start_charging_on_tag_deny);
		}
	}else{
		if(state != eOCPP_CP_STATUS_RESERVED){
			ocpp_finishing_session = false; // Only relevant if state is Finishing
			start_charging_on_tag_accept(id_tag);
		}else{
			reserved_on_tag_accept(id_tag, reservation_info != NULL ? reservation_info->id_tag : id_tag);
		}
	}

	return;
error:
	if(err == eOCPPJ_NO_ERROR){
		ESP_LOGE(TAG, "Remote start transaction error reached without known error");
		err = eOCPPJ_ERROR_INTERNAL;
	}

	cJSON * ocpp_error = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(ocpp_error == NULL){
		ESP_LOGE(TAG, "Unable to create call error for RemoteStartTransaction.req");
	}else{
		send_call_reply(ocpp_error);
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
		}
		return;
	}

	cJSON * response;
	bool stop_charging_accepted = false;
	if(transaction_id != NULL && *transaction_id == transaction_id_json->valueint){
		ESP_LOGI(TAG, "Stop request id matches ongoing transaction id");
		stop_charging_accepted = true;
		response = ocpp_create_remote_stop_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_ACCEPTED);
	}
	else{
		ESP_LOGW(TAG, "Stop request id does not match any ongoing transaction id. ongoing: %d requested: %d", transaction_id != NULL ? *transaction_id : -1,
			transaction_id_json->valueint);

		response = ocpp_create_remote_stop_transaction_confirmation(unique_id, OCPP_REMOTE_START_STOP_STATUS_REJECTED);
	}

	if(response == NULL){
		ESP_LOGE(TAG, "Unable to create remote stop transaction confirmation");
	}
	else{
		send_call_reply(response);
	}

	if(stop_charging_accepted){
	        sessionHandler_InitiateResetChargeSession();
		SetAuthorized(false);
		chargeSession_SetStoppedReason(eOCPP_REASON_REMOTE);
	}
	//TODO: "[...]and, if applicable, unlock the connector"
}

static void change_availability_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Request to change availability");
	pending_change_availability = false;

	char err_str[128] = {0};

	int connector_id;
	enum ocppj_err_t err = ocppj_get_int_field(payload, "connectorId", true, &connector_id, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_ERROR)
		goto error;

	if(connector_id < 0 || connector_id > CONFIG_OCPP_NUMBER_OF_CONNECTORS){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		strcpy(err_str, "Expected connectorId to name a valid connector");
		goto error;
	}

	char * availability_type;
	err = ocppj_get_string_field(payload, "type", true, &availability_type, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_ERROR)
		goto error;

	if(ocpp_validate_enum(availability_type, true, 2,
				OCPP_AVAILABILITY_TYPE_INOPERATIVE,
				OCPP_AVAILABILITY_TYPE_OPERATIVE) != 0){
		err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		strcpy(err_str, "Expected 'type' to be a vaild AvailabilityType");
		goto error;
	}

	bool new_available = strcmp(availability_type, OCPP_AVAILABILITY_TYPE_OPERATIVE) == 0;
	bool old_available = storage_Get_availability_ocpp();

	cJSON * reply = NULL;
	if(new_available != old_available)
	{
		enum ocpp_cp_status_id ocpp_state = get_ocpp_state(MCU_GetChargeOperatingMode(), MCU_GetChargeMode());
		if(new_available == true || (ocpp_state != eOCPP_CP_STATUS_CHARGING && ocpp_state != eOCPP_CP_STATUS_SUSPENDED_EV && ocpp_state != eOCPP_CP_STATUS_SUSPENDED_EVSE)){
			if(change_availability(new_available) == 0)
			{
				reply = ocpp_create_change_availability_confirmation(unique_id, OCPP_AVAILABILITY_STATUS_ACCEPTED);
			}
			else
			{
				err = eOCPPJ_ERROR_INTERNAL;
				strcpy(err_str, "Unable to update availability");
				goto error;
			}
		}
		// "When a transaction is in progress Charge Point SHALL respond with availability status 'Scheduled' to indicate that it is scheduled to occur after the transaction has finished"
		else{
			pending_change_availability = true;
			pending_change_availability_state = new_available;
			reply = ocpp_create_change_availability_confirmation(unique_id, OCPP_AVAILABILITY_STATUS_SCHEDULED);
		}
	}else{
			reply = ocpp_create_change_availability_confirmation(unique_id, OCPP_AVAILABILITY_STATUS_ACCEPTED);
	}

	if(reply == NULL){
		ESP_LOGE(TAG, "Unable to create create .conf for change availability");
	}else{
		send_call_reply(reply);
	}

	ESP_LOGI(TAG, "Change availability .req complete %s->%s", old_available ? "operative" : "inoperative", new_available ? "operative" : "inoperative");
	return;

error:
	if(err == eOCPPJ_NO_ERROR){
		ESP_LOGE(TAG, "Change availability reached error exit without error");
		err = eOCPPJ_ERROR_INTERNAL;
	}

	const char * err_code = ocppj_error_code_from_id(err);
	ESP_LOGE(TAG, "Change availability error: [%s] '%s'", err_code, err_str);

	cJSON * ocpp_err = ocpp_create_call_error(unique_id, err_code, err_str, NULL);
	if(ocpp_err == NULL){
		ESP_LOGE(TAG, "Unable to create call error for change configuration");
	}else{
		send_call_reply(ocpp_err);
	}
}

bool change_availability_if_pending(bool allowed_new_state){
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

		switch(old_state){
		case eOCPP_CP_STATUS_PREPARING:
			break;
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
			stop_transaction(new_state);
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
	case eOCPP_CP_STATUS_PREPARING: // When adding to Preparing transition, make sure to update reserved_on_tag_accept
		ESP_LOGI(TAG, "OCPP STATE PREPARING");

		transition_to_preparing();

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
			start_transaction();
			break;
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
			start_transaction();
			break;
		case eOCPP_CP_STATUS_CHARGING:
			break;
		case eOCPP_CP_STATUS_SUSPENDED_EV:
			resume_if_allowed();
			break;
		case eOCPP_CP_STATUS_UNAVAILABLE:
		case eOCPP_CP_STATUS_FAULTED:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Suspended EVSE", old_state);
		}
		break;
	case eOCPP_CP_STATUS_FINISHING:
		ESP_LOGI(TAG, "OCPP STATE FINISHING");
		ocpp_finishing_session = true;
		SetAuthorized(false);

		switch(old_state){
		case eOCPP_CP_STATUS_PREPARING:
			break;
		case eOCPP_CP_STATUS_CHARGING:
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
			stop_transaction(new_state);
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
			stop_transaction(new_state);
			SetAuthorized(false);
		case eOCPP_CP_STATUS_SUSPENDED_EV:
		case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		case eOCPP_CP_STATUS_FINISHING:
			break;
		case eOCPP_CP_STATUS_RESERVED:
			free(reservation_info);
			reservation_info = NULL;
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
			break;
		case eOCPP_CP_STATUS_RESERVED:
			free(reservation_info);
			reservation_info = NULL;
			break;
		case eOCPP_CP_STATUS_UNAVAILABLE:
			break;
		default:
			ESP_LOGE(TAG, "Invalid state transition from %d to Faulted", old_state);
		}
		break;
	}

	ocpp_send_status_notification(new_state, OCPP_CP_ERROR_NO_ERROR, NULL, NULL, NULL, false, false);

	if(new_state == eOCPP_CP_STATUS_AVAILABLE || new_state == eOCPP_CP_STATUS_FINISHING)
		change_availability_if_pending(false);

	ocpp_old_state = new_state;
}

static bool has_new_id_token(){
	return (NFCGetTagInfo().tagIsValid == true) && (chargeSession_Get().StoppedByRFID == false) && (pending_ocpp_authorize == false);
}

static void handle_available(){
	if(!pending_ocpp_authorize && has_new_id_token()){
		authorize(NFCGetTagInfo(), start_charging_on_tag_accept, start_charging_on_tag_deny);
		NFCTagInfoClearValid();
	}
}

static void handle_preparing(){
	/**
	 * From ocpp protocol 1.6 section 3.6:
	 * "Transaction starts at the point that all conditions for charging are met,
	 * for instance, EV is connected to Charge Point and user has been authorized."
	 */
	if((MCU_GetChargeMode() == eCAR_CONNECTED || MCU_GetChargeMode() == eCAR_CHARGING) && (isAuthorized || !storage_Get_AuthenticationRequired())){
		ESP_LOGI(TAG, "User actions complete; Attempting to start charging");

		//Use standalone until changed by ocpp_smart_charging
		ocpp_max_limit = storage_Get_StandaloneCurrent();
		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, ocpp_max_limit);
		if(ret == MsgWriteAck){
			ESP_LOGI(TAG, "Max Current set to %f", storage_Get_StandaloneCurrent());
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
		}
	}else if(has_new_id_token()){

		if(isAuthorized){
			pending_ocpp_authorize = true;

			MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING); // TODO: Improve signaling to user.
			if(ret == MsgWriteAck)
				ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
			else
				ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

			ocpp_authorize_compare(id_token_from_tag(NFCGetTagInfo().idAsString), NULL,
					chargeSession_GetAuthenticationCode(), chargeSession_Get().parent_id,
					cancel_authorization_on_tag_accept, cancel_authorization_on_tag_deny);
		}else{
			authorize(NFCGetTagInfo(), start_charging_on_tag_accept, start_charging_on_tag_deny);
		}
		NFCTagInfoClearValid();

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
			sessionHandler_InitiateResetChargeSession();
			chargeSession_ClearAuthenticationCode();

			if(reservation_info != NULL){
				ESP_LOGW(TAG, "Connection timeout is transaction related");
				free(reservation_info);
				reservation_info = NULL;
			}
		}
		else{
			ESP_LOGI(TAG, "Waiting for cable to connect... Timeout: %" PRIu64 "/%" PRIu32, time(NULL) - preparing_started, storage_Get_ocpp_connection_timeout());
		}
	}
}

static void handle_charging(){
	if(!pending_ocpp_authorize && has_new_id_token()){
		ESP_LOGI(TAG, "Checking if new tag is authorized to cancel charging");
		ocpp_authorize_compare(id_token_from_tag(NFCGetTagInfo().idAsString), NULL,
						chargeSession_Get().AuthenticationCode, chargeSession_Get().parent_id,
						stop_charging_on_tag_accept, stop_charging_on_tag_deny);
		NFCTagInfoClearValid();
	}
}

static void handle_finishing(){
	if(!pending_ocpp_authorize && has_new_id_token()){
		authorize(NFCGetTagInfo(), start_charging_on_tag_accept, start_charging_on_tag_deny);
		NFCTagInfoClearValid();
		ocpp_finishing_session = false;
	}
}

static void handle_reserved(){
	if(!pending_ocpp_authorize && has_new_id_token()){

		MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
		if(ret == MsgWriteAck)
			ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
		else
			ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

		ocpp_authorize_compare(id_token_from_tag(NFCGetTagInfo().idAsString), NULL,
						reservation_info->id_tag, reservation_info->parent_id_tag,
						reserved_on_tag_accept, reserved_on_tag_deny);
		NFCTagInfoClearValid();

	}else if(time(NULL) > reservation_info->expiry_date){
		ESP_LOGW(TAG, "Canceling reservation due to expiration");
		free(reservation_info);
		reservation_info = NULL;

	}else if(!storage_Get_AuthenticationRequired()){
		ESP_LOGW(TAG, "Canceling reservation due to authorization no longer required");
		free(reservation_info);
		reservation_info = NULL;
	}
}

static uint32_t ocpp_notified_warnings = 0x00000000;
static uint16_t last_emeter_alarm = 0x0000;
static uint16_t ocpp_notified_emeter_alarm = 0x0000;

// Masks used to convert an mcu warning to an ocpp ChargePointErrorCode
enum ocpp_mcu_error_code{
	/* WARNING_REBOOT*/
	eOCPP_MCU_CONNECTOR_LOCK_FAILURE = WARNING_SERVO,
	eOCPP_MCU_EV_COMMUNICATION_ERROR = WARNING_PILOT_STATE | WARNING_PILOT_LOW_LEVEL | WARNING_PILOT_NO_PROXIMITY,
	eOCPP_MCU_GROUND_FAILURE = WARNING_RCD,
	eOCPP_MCU_HIGH_TEMPERATURE = WARNING_TEMPERATURE,
	eOCPP_MCU_INTERNAL_ERROR = WARNING_TEMPERATURE_ERROR | WARNING_UNEXPECTED_RELAY | WARNING_FPGA_WATCHDOG | WARNING_MAX_SESSION_RESTART | WARNING_DISABLED | WARNING_FPGA_VERSION | WARNING_NO_VOLTAGE_L1 | WARNING_NO_VOLTAGE_L2_L3 | WARNING_12V_LOW_LEVEL,
	eOCPP_MCU_OTHER_ERROR = WARNING_HUMIDITY | WARNING_O_PEN,
	eOCPP_MCU_OVER_CURRENT_FAILURE = WARNING_OVERCURRENT_INSTALLATION | WARNING_CHARGE_OVERCURRENT,
	/* eOCPP_MCU_OVER_VOLTAGE = , */
	eOCPP_MCU_POWER_METER_FAILURE = WARNING_EMETER_NO_RESPONSE | WARNING_EMETER_LINK | WARNING_EMETER_ALARM,
	eOCPP_MCU_POWER_SWITCHFAILURE = WARNING_NO_SWITCH_POW_DEF,
	/* eOCPP_MCU_UNDER_VOLTAGE = ,*/
};

// TODO: Update with errors that need to be cleared before exiting faulted state
#define MCU_WARNING_TRANSITION_FAULTED (WARNING_RCD | WARNING_CLEAR_REPLUG | WARNING_CLEAR_DISCONNECT_TRANSITION | WARNING_CLEAR_DISCONNECT_TRANSITION | WARNING_CLEAR_DISCONNECT)

static void handle_faulted(){
	if((ocpp_notified_warnings & MCU_WARNING_TRANSITION_FAULTED) == false){
		sessionHandler_OcppTransitionFromFaulted();
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
		handle_reserved();
		break;
	case eOCPP_CP_STATUS_UNAVAILABLE:
		break;
	case eOCPP_CP_STATUS_FAULTED:
		handle_faulted();
		break;
	}
}

bool weak_connection = false;
time_t weak_connection_timestamp = 0;
time_t weak_connection_check_timestamp = 0;

#define WEAK_CONNECTION_SEND_INTERVAL 300 // The minimum duration between two status notifications with weak signal warning
#define WEAK_CONNECTION_CHECK_INTERVAL 10 // The minimum duration between each check for weak signal

char vendor_error_code[16];
static void handle_warnings(enum ocpp_cp_status_id * state, uint32_t warning_mask){
	if(get_registration_status() != eOCPP_REGISTRATION_ACCEPTED)
		return;

	uint32_t new_warning = warning_mask & ~ocpp_notified_warnings;
	ocpp_notified_warnings = warning_mask; // Allow new notification for warnings that has now been cleared

	//TODO: Add description to status notifications
	if(new_warning){ // A warning that has not been notified earlier
		sprintf(vendor_error_code, "%" PRIu32, new_warning);

		if(new_warning & MCU_WARNING_TRANSITION_FAULTED){
			sessionHandler_OcppTransitionToFaulted();
			*state = eOCPP_CP_STATUS_FAULTED;
		}

		if(new_warning & eOCPP_MCU_CONNECTOR_LOCK_FAILURE){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_CONNECTOR_LOCK_FAILURE,
						"Unable to lock cable to charging station", NULL, vendor_error_code, true, false);
		}
		if(new_warning & eOCPP_MCU_EV_COMMUNICATION_ERROR){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_EV_COMMUNICATION_ERROR,
						"Vehicle communication error, Inspect cable and car", NULL, vendor_error_code, true, false);
		}
		if(new_warning & eOCPP_MCU_GROUND_FAILURE){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_GROUND_FAILURE,
						"Ground fault, Try reconnecting cable", NULL, vendor_error_code, true, false);
		}
		if(new_warning & eOCPP_MCU_HIGH_TEMPERATURE){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_HIGH_TEMPERATURE,
						"High temperature, Let charging station cool down", NULL, vendor_error_code, true, false);
		}
		if(new_warning & eOCPP_MCU_INTERNAL_ERROR){
			if(new_warning & WARNING_TEMPERATURE_ERROR){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Temperature sensor error, Try restarting CP", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_UNEXPECTED_RELAY){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Relay not configured", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_FPGA_WATCHDOG){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"FPGA watchdog triggered", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_MAX_SESSION_RESTART){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Charging reset too many times, Try reconnecting", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_DISABLED && storage_Get_availability_ocpp()){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Configured as inactive, check zaptec portal", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_FPGA_VERSION){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Incorrect FPGA version", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_RCD){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"RCD warning", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_NO_VOLTAGE_L1 || new_warning & WARNING_NO_VOLTAGE_L2_L3){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Charge output fuse is blown", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_12V_LOW_LEVEL){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_INTERNAL_ERROR,
							"Low voltage in charge station component", NULL, vendor_error_code, true, false);
			}
		}
		if(new_warning & eOCPP_MCU_OTHER_ERROR){
			if(new_warning & WARNING_O_PEN){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_OTHER_ERROR,
							"Abnormal sypply voltage detected", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_HUMIDITY){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_OTHER_ERROR,
							"Moisture in the charging station", NULL, vendor_error_code, true, false);
			}
		}
		if(new_warning & eOCPP_MCU_OVER_CURRENT_FAILURE){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_OVER_CURRENT_FAILURE,
						"The vehicle has drawn more current than allowed", NULL, vendor_error_code, true, false);
		}
		if(new_warning & eOCPP_MCU_POWER_METER_FAILURE){

			if(new_warning & WARNING_EMETER_NO_RESPONSE || new_warning & WARNING_EMETER_LINK){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_POWER_METER_FAILURE,
							"Emeter communication error", NULL, vendor_error_code, true, false);
			}

			if(new_warning & WARNING_EMETER_ALARM){
				ocpp_send_status_notification(*state, OCPP_CP_ERROR_POWER_METER_FAILURE,
							"Charging station limits have been exceeded", NULL, vendor_error_code, true, false);
			}
		}
		if(new_warning & eOCPP_MCU_POWER_SWITCHFAILURE){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_POWER_SWITCH_FAILURE,
						"The charging station has not been fully configured", NULL, vendor_error_code, true, false);
		}
	}

	uint16_t new_emeter_alarm = last_emeter_alarm & ~ocpp_notified_emeter_alarm;
	ocpp_notified_emeter_alarm = last_emeter_alarm;

	//TODO: test with emeter alarm
	if(new_emeter_alarm){
		sprintf(vendor_error_code, "%u", new_emeter_alarm);
		if(new_emeter_alarm & (EMETER_PARAM_STATUS_OV_VRMSA | EMETER_PARAM_STATUS_OV_VRMSB | EMETER_PARAM_STATUS_OV_VRMSC)){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_OVER_VOLTAGE,
						"Reported by emeter", NULL, vendor_error_code, true, false);
		}
		if(new_emeter_alarm & (EMETER_PARAM_STATUS_UN_VRMSA | EMETER_PARAM_STATUS_UN_VRMSB | EMETER_PARAM_STATUS_UN_VRMSC)){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_UNDER_VOLTAGE,
						"Reported by emeter", NULL, vendor_error_code, true, false);
		}
		if(new_emeter_alarm & (EMETER_PARAM_STATUS_OV_IRMSA | EMETER_PARAM_STATUS_OV_IRMSB | EMETER_PARAM_STATUS_OV_IRMSC)){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_OVER_CURRENT_FAILURE,
						"Reported by emeter", NULL, vendor_error_code, true, false);
		}
		if(new_emeter_alarm & EMETER_PARAM_STATUS_OV_TEMP){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_HIGH_TEMPERATURE,
						"Reported by emeter", NULL, vendor_error_code, true, false);
		}
		if(new_emeter_alarm & EMETER_PARAM_STATUS_UN_TEMP){
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_OTHER_ERROR,
						"Low temperature reported by emeter", NULL, vendor_error_code, true, false);
		}
	}

	time_t now = time(NULL);
	if((!weak_connection || now > weak_connection_timestamp + WEAK_CONNECTION_SEND_INTERVAL)
		&& now > weak_connection_check_timestamp + WEAK_CONNECTION_CHECK_INTERVAL){

		weak_connection_check_timestamp = now;

		if((storage_Get_CommunicationMode() == eCONNECTION_WIFI && network_WifiSignalStrength() <= -80)
			|| (storage_Get_CommunicationMode() == eCONNECTION_LTE && GetCellularQuality() <= 20)){

			weak_connection = true;
		}else{
			weak_connection = false;
		}

		if(weak_connection){
			ESP_LOGW(TAG, "Weak wireless");
			weak_connection_timestamp = now;
			ocpp_send_status_notification(*state, OCPP_CP_ERROR_WEAK_SIGNAL, NULL, NULL, NULL, true, false);
		}
	}

	if(GetNewReaderFailure()){
		ocpp_send_status_notification(*state, OCPP_CP_ERROR_READER_FAILURE, NULL, NULL, NULL, true, false);
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
	offlineSession_AppendLogStringWithInt("3 NrOfFiles: ", nrOfOfflineSessionFiles);
	int nrOfSentSessions = 0;
	int fileNo;
	for (fileNo = 0; fileNo < nrOfOfflineSessionFiles; fileNo++)
	{
		memset(completedSessionString,0, LOG_STRING_SIZE);

		int fileToUse = offlineSession_FindOldestFile();
		offlineSession_AppendLogStringWithInt("3 fileToUse: ", fileToUse);

		OCMF_CompletedSession_CreateNewMessageFile(fileToUse, completedSessionString);

		int sessionLength = 0;
		if(completedSessionString == NULL)
		{
			offlineSession_AppendLogString("3 CSess = NULL");
			publish_debug_message_event("Empty CompletedSession", cloud_event_level_warning);
		}
		else
		{
			sessionLength = strlen(completedSessionString);
			if(sessionLength < 30)
				publish_debug_message_event("Short CompletedSession", cloud_event_level_warning);

			offlineSession_AppendLogStringWithInt("3 CSessLen: ", sessionLength);
		}


		/// This transmission has been made a blocking call
		int ret = publish_debug_telemetry_observation_CompletedSession(completedSessionString);
		if (ret == 0)
		{
			offlineSession_AppendLogString("3 CS sent OK");
			offlineSession_AppendLogLength();

			if((storage_Get_DiagnosticsMode() == eALWAYS_SEND_SESSION_DIAGNOSTICS) || (completedSessionString == NULL) || (sessionLength < 30))
			{
				publish_debug_telemetry_observation_Diagnostics(offlineSession_GetLog());
			}

			nrOfSentSessions++;
			/// Sending succeeded -> delete file from flash
			offlineSession_delete_session(fileToUse);

			ESP_LOGW(TAG,"Sent CompletedSession: %i/%i", nrOfSentSessions, nrOfOfflineSessionFiles);
		}
		else
		{
			publish_debug_message_event("Failed sending CompletedSession", cloud_event_level_warning);

			offlineSession_AppendLogString("3 CS send FAIL");
			offlineSession_AppendLogLength();
			publish_debug_telemetry_observation_Diagnostics(offlineSession_GetLog());

			/// Send to Diagnostics
			publish_debug_telemetry_observation_Diagnostics(completedSessionString);
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

static uint16_t autoClearLastCount = 0;
static uint32_t autoClearLastTimeout = 0;

static uint16_t memoryDiagnosticsFrequency = 0;
void SetMemoryDiagnosticsFrequency(uint16_t freq)
{
	memoryDiagnosticsFrequency = freq;
}

static uint16_t mcuDiagnosticsFrequency = 0;
void SetMCUDiagnosticsFrequency(uint16_t freq)
{
	mcuDiagnosticsFrequency = freq;
}


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
    enum  ChargerOperatingMode previousChargeOperatingMode = CHARGE_OPERATION_STATE_UNINITIALIZED;
    enum CommunicationMode networkInterface = eCONNECTION_NONE;

#ifndef CONFIG_ZAPTEC_MCU_APPLICATION_ONLY

    uint32_t mcuDebugCounter = 0;
    uint32_t previousDebugCounter = 0;
    uint32_t mcuDebugErrorCount = 0;

#endif

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

    bool fileSystemOk = false;

    uint32_t previousWarnings = 0;
    bool firstTimeAfterBoot = true;
    uint8_t countdown = 5;

    boot_timestamp = time(NULL);

    // Prepare for incomming ocpp messages
    attach_call_cb(eOCPP_ACTION_CHANGE_AVAILABILITY_ID, change_availability_cb, NULL);
    attach_call_cb(eOCPP_ACTION_REMOTE_START_TRANSACTION_ID, remote_start_transaction_cb, NULL);
    attach_call_cb(eOCPP_ACTION_REMOTE_STOP_TRANSACTION_ID, remote_stop_transaction_cb, NULL);
    attach_call_cb(eOCPP_ACTION_RESERVE_NOW_ID, reserve_now_cb, NULL);
    attach_call_cb(eOCPP_ACTION_CANCEL_RESERVATION_ID, cancel_reservation_cb, NULL);

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

    ///Ensure MCU is up and running before continuing to ensure settings can be written
    int MCUtimeout = 15;
    while ((!MCU_IsReady() && MCUtimeout > 0))
	{
    	MCUtimeout--;
		ESP_LOGW(TAG, "Waiting for MCU: %i", MCUtimeout);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
    chargeController_Init();

    offlineSession_Init();
    ocpp_transaction_set_callbacks(
	    start_transaction_response_cb, start_transaction_error_cb,
	    stop_transaction_response_cb, NULL,
	    NULL, NULL);

    /// Check for corrupted "files"-partition
    fileSystemOk = offlineSession_CheckAndCorrectFilesSystem();

    ESP_LOGW(TAG, "FileSystemOk: %i Correction needed: %i", fileSystemOk, offlineSession_FileSystemCorrected());

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
			if(maxCount > 0)
			{
				snprintf(sessionString, sizeof(sessionString),"MaxOfflineSessions: %i", maxCount);
				ESP_LOGI(TAG, "%s", sessionString);
				publish_debug_telemetry_observation_Diagnostics(sessionString);
			}
		}


		networkInterface = connectivity_GetActivateInterface();

#ifndef CONFIG_ZAPTEC_MCU_APPLICATION_ONLY

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
				ESP_LOGE(TAG, "ESP resetting due to mcuDebugCounter: %" PRIi32 "", mcuDebugCounter);
				publish_debug_message_event("mcuDebugCounter reset", cloud_event_level_warning);

				storage_Set_And_Save_DiagnosticsLog("#4 MCU debug counter stopped incrementing");

				vTaskDelay(5000 / portTICK_PERIOD_MS);

				esp_restart();
			}
		}

#endif

		if(networkInterface == eCONNECTION_NONE)
		{
			if((onCounter % 10) == 0)
				ESP_LOGI(TAG, "CommunicationMode == eCONNECTION_NONE");

		}


		if(chargeSession_HasNewSessionId() == true)
		{
			if(chargecontroller_IsPauseBySchedule() == true)
			{
				///In order to avoid receiving a start command when connecting during schedule paused state,
				/// then when a new sessionId is received, the charger must send the paused state before
				/// replying the new SessionID back to Cloud. The delay does not guarantee order of deliver(!)
				publish_uint32_observation(ParamChargeOperationMode, CHARGE_OPERATION_STATE_PAUSED);
				vTaskDelay(1000 / portTICK_PERIOD_MS);
			}

			int ret = publish_string_observation(SessionIdentifier, chargeSession_GetSessionId());
			ESP_LOGI(TAG, "Sending sessionId: %s (%i)", chargeSession_GetSessionId(), ret);
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

		// Handle ocpp state if session type is ocpp
		if(storage_Get_session_controller() == eSESSION_OCPP){
			if(ocpp_startup && ocpp_transaction_is_ready()){
				// If this is the first loop where CP is registered, then we may have just rebooted and need to sync state with storage.
				ocpp_startup = false;

				ESP_LOGI(TAG, "Check if active transaction was on file before CS accepted boot");
				if(ocpp_transaction_find_active_entry(1) != -1){
					ESP_LOGW(TAG, "Transaction was on file");

					ocpp_id_token stored_token;
					transaction_id = malloc(sizeof(int));

					if(transaction_id != NULL){
						if(ocpp_transaction_load_into_session(&transaction_start, stored_token, transaction_id, &transaction_id_is_valid) != ESP_OK){
							ESP_LOGE(TAG, "Expected active entry, but unable to load into session. Did it just end?");

						}else{
							chargeSession_SetAuthenticationCode(stored_token);

							ocpp_set_active_transaction_id(transaction_id);
							ocpp_set_transaction_is_active(true, transaction_start);

							if(storage_Get_ocpp_meter_value_sample_interval() > 0)
								start_sample_interval(eOCPP_CONTEXT_OTHER);

							enum ChargerOperatingMode operating_mode = MCU_GetChargeOperatingMode();

							if(operating_mode != CHARGE_OPERATION_STATE_CHARGING && operating_mode != CHARGE_OPERATION_STATE_PAUSED){

								enum ocpp_reason_id reason = eOCPP_REASON_OTHER;
								/*
								 * We use a higher max counter value here than in stop_transaction
								 * to detect reboot, as we expect reboot to be the cause, as the esp also reset.
								 * We also account for some time from boot to sessionHandler starting.
								 */
								if(MCU_GetDebugCounter() < 300){
									ESP_LOGW(TAG, "Resent reboot caused transaction to end");
									if(MCU_GetResetSource() & 1<<2){ // brownout
										reason = eOCPP_REASON_POWER_LOSS;
									}else{
										reason = eOCPP_REASON_REBOOT;
									}
								}else{
									reason = eOCPP_REASON_OTHER; // May be overwritten by stop_transaction
								}

								chargeSession_SetStoppedReason(reason);
								stop_transaction(eOCPP_CP_STATUS_FINISHING);
							}
						}
					}else{
						ESP_LOGE(TAG, "Unable to allocate memory for transaction id during loading of stored transaction");
					}

				}else{
					ESP_LOGI(TAG, "No transaction on file");
				}
			}

			enum ocpp_cp_status_id ocpp_new_state = get_ocpp_state(chargeOperatingMode, currentCarChargeMode);
			handle_warnings(&ocpp_new_state, MCU_GetWarnings());

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

		//If we are charging when going from offline to online, send a stop command to change the state to requesting.
		//This will make the Cloud send a new start command with updated current to take us out of offline current mode
		//Check the requestCurrentWhenOnline to ensure we don't send at every token refresh, and only in system mode.
		if((previousIsOnline == false) && (isOnline == true) && offlineHandler_IsRequestingCurrentWhenOnline())
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


		// Check if car connecting -> start a new session
		if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode <= CHARGE_OPERATION_STATE_DISCONNECTED) && (sessionResetMode == eSESSION_RESET_NONE))
		{
			offlineSession_ClearLog();
			chargeSession_Start();

			/// Flag event warning as diagnostics if energy in OCMF Begin does not match OCMF End in previous session
			if(isOnline && (OCMF_GetEnergyFault() == true))
			{
				publish_debug_message_event("OCMF energy fault", cloud_event_level_warning);
			}
		}


		/// MQTT connected and pingReply not in offline state
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
				//When controlled by schedule or startDelayCounter, do not resend requests
				if(chargecontroller_IsPauseBySchedule() == false)
					resendRequestTimer++;

				ESP_LOGI(TAG, "CHARGE STATE resendTimer: %" PRId32 "/%" PRId32 "", resendRequestTimer, resendRequestTimerLimit);
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
					ESP_LOGI(TAG, "System mode: Waiting to declare offline: %" PRId32 "/%" PRId32 "", offlineTime, recordedPulseInterval * 2);
				}
			}
			else
			{
				//OfflineMode is only for System use
				offlineMode = false;
			}
		}


		bool stoppedByRfid = chargeSession_Get().StoppedByRFID;

		if(((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (authorizationRequired == true)) && storage_Get_session_controller() != eSESSION_OCPP)
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
				if((isOnline) && (chargeSession_IsAuthenticated() == false))
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


		///When warnings are cleared - like O-PEN warning - make sure it get a new wake-up sequence in case it is sleeping.
		uint32_t warnings = MCU_GetWarnings();
		if((warnings == 0) && (previousWarnings != 0))
		{
			sessionHandler_ClearCarInterfaceResetConditions();
			ESP_LOGW(TAG, "ClearedInterfaceResetCondition");
		}
		previousWarnings = warnings;


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
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			//Clear RCD error counter on disconnect to allow recount
			errorCountAB = 0;

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

			chargeSession_Clear();

			NFCClearTag();

			//Ensure the authentication status is cleared at disconnect
			i2cClearAuthentication();

			chargeController_ClearRandomStartDelay();
			chargeController_SetHasBeenDisconnected();
			chargeController_SetRandomStartDelay();
		}
		

		//If the FinalStopActive bit is set when a car disconnect, make sure to clear the status value used by Cloud
		if((chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED) && (GetFinalStopActiveStatus() == true))
		{
			SetFinalStopActiveStatus(0);
		}

		//Set flag for the periodic OCMF message to show that charging has occured within an
		// interval so that energy message must be sent
		if((chargeOperatingMode != CHARGE_OPERATION_STATE_CHARGING) && (previousChargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING))
		{
			hasCharged = true;
			ESP_LOGW(TAG, " ### No longer charging but must report remaining energy ###");
		}


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
				ESP_LOGI(TAG,"LTE: %d %%  DataInterval: %" PRId32 "  Pulse: %" PRId32 "/%" PRId32 "", GetCellularQuality(), dataInterval, pulseCounter, pulseInterval);
			}
			else if (networkInterface == eCONNECTION_WIFI)
			{
				if (esp_wifi_sta_get_ap_info(&wifidata)==0)
					rssi = wifidata.rssi;
				else
					rssi = 0;

				ESP_LOGI(TAG,"WIFI: %d dBm  DataInterval: %" PRId32 "  Pulse: %" PRId32 "/%" PRId32 "", rssi, dataInterval, pulseCounter, pulseInterval);
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

				publish_debug_telemetry_observation_capabilities();
				publish_debug_telemetry_observation_StartUpParameters();
				publish_debug_telemetry_observation_all(rssi);
				publish_debug_telemetry_observation_local_settings();
				publish_debug_telemetry_observation_power();

				if(chargeController_IsScheduleActive())
					publish_debug_telemetry_observation_TimeAndSchedule(0x7);


				sessionHandler_SendFPGAInfo();
				sessionHandler_SendMIDStatus();

				if(offlineSession_FileSystemCorrected() == true)
				{
					ESP_LOGW(TAG,"Event content: %s", offlineSession_GetDiagnostics());
					if(offlineSession_FileSystemVerified())
						publish_debug_message_event("File system corrected OK", cloud_event_level_warning);
					else
						publish_debug_message_event("File system correction FAILED", cloud_event_level_warning);

					publish_debug_telemetry_observation_Diagnostics(offlineSession_GetDiagnostics());
				}

				/// If we start up after an unexpected reset. Send and clear the diagnosticsLog.
				if(storage_Get_DiagnosticsLogLength() > 0)
				{
					publish_debug_telemetry_observation_DiagnosticsLog();
					storage_Clear_And_Save_DiagnosticsLog();
				}

				if(reportChargingStateCommandSent == true)
				{
					ReInitParametersForCloud();
					publish_debug_telemetry_observation_PulseInterval(recordedPulseInterval);
					reportChargingStateCommandSent = false;
				}

				//Since they are synced on start they no longer need to be sent at every startup. Can even cause inconsistency.
				//publish_debug_telemetry_observation_cloud_settings();

				startupSent = true;
			}

			// Send MID status update if status changed
			sessionHandler_SendMIDStatusUpdate();

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


			if(chargeSession_GetFileError() == true)
			{
				ESP_LOGW(TAG, "Sending file error SequenceLog: \r\n%s", offlineSession_GetLog());
				publish_debug_telemetry_observation_Diagnostics(offlineSession_GetLog());

				publish_debug_message_event("Could not create sessionfile", cloud_event_level_warning);
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
				logCurrentsCounter++;
				//This turns off high frequency logging if interval is below 5 min

				if((logCurrentStop > 0) && (logCurrentsCounter > logCurrentStop))
				{
					logCurrents = false;
					logCurrentsCounter = 0;
				}

				if(logCurrentsCounter % logCurrentsInterval == 0)
				{
					publish_debug_telemetry_observation_power();
					logCurrentsCounter = 0;
				}

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
						last_emeter_alarm = rxMsg.data[0] + (rxMsg.data[1] << 8);

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

			uint32_t acTimeout = 0;
			uint16_t acCount = 0, acTotalCount = 0;
			
			// Send event log entry if auto clear on MCU occurs or if a reset of the timeout occurs
			if (MCU_GetAutoClearStatus(&acTimeout, &acCount, &acTotalCount) && 
					(acTotalCount != autoClearLastCount || acTimeout < autoClearLastTimeout)) {

				char buf[64];
				snprintf(buf, sizeof (buf), "AutoClear: %" PRIu32 " / %d / %d", acTimeout, acCount, acTotalCount);

				publish_debug_message_event(buf, cloud_event_level_warning);

				ESP_LOGI(TAG, "AutoClear Timeout: %" PRIu32 " CurrenTime: %d TotalClears: %d", acTimeout, acCount, acTotalCount);

				autoClearLastTimeout = acTimeout;
				autoClearLastCount = acTotalCount;
			}

			if(onTime % 15 == 0)//15
			{
				struct MqttDataDiagnostics mqttDiag = MqttGetDiagnostics();
				char buf[150]={0};
				snprintf(buf, sizeof(buf), "%" PRId32 " MQTT data: Rx: %" PRId32 " %" PRId32 " #%" PRId32 " - Tx: %" PRId32 " %" PRId32 " #%" PRId32 " - Tot: %" PRId32 " (%d)", onTime, mqttDiag.mqttRxBytes, mqttDiag.mqttRxBytesIncMeta, mqttDiag.nrOfRxMessages, mqttDiag.mqttTxBytes, mqttDiag.mqttTxBytesIncMeta, mqttDiag.nrOfTxMessages, (mqttDiag.mqttRxBytesIncMeta + mqttDiag.mqttTxBytesIncMeta), (int)((1.1455 * (mqttDiag.mqttRxBytesIncMeta + mqttDiag.mqttTxBytesIncMeta)) + 4052.1));//=1.1455*C11+4052.1
				//ESP_LOGI(TAG, "**** %s ****", buf);

				if(onTime % 7200 == 0)
				{
					//Only publish if activated by command
					if(GetDatalog())
						publish_debug_telemetry_observation_Diagnostics(buf);

					MqttDataReset();
					ESP_LOGW(TAG, "**** Hourly MQTT data reset ****");
				}
			}

			/// Send available DMA memory once or hourly
			if (memoryDiagnosticsFrequency > 0)
			{
				if(onTime % memoryDiagnosticsFrequency == 0)
				{
					if(memoryDiagnosticsFrequency == 1)
						memoryDiagnosticsFrequency = 0;

					char membuf[70];

					size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
					size_t min_dma = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
					size_t blk_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

					snprintf(membuf, 70, "DMA memory free: %d, min: %d, largest block: %d", free_dma, min_dma, blk_dma);
					publish_debug_telemetry_observation_Diagnostics(membuf);
				}
			}

			/// Send MCU diagnostics once or periodically
			if (mcuDiagnosticsFrequency > 0)
			{
				if(onTime % mcuDiagnosticsFrequency == 0)
				{
					if(mcuDiagnosticsFrequency == 1)
						mcuDiagnosticsFrequency = 0;

					sessionHandler_SendMCUSettings();
				}
			}


			NotificationHandler();

			/// For chargers with x804 RCD, this prints out diagnostics every 24 hours if there has been any faults.
			if(errorCountABCTotal > 0)
			{
				if(onTime % 86400 == 0)
				{
					char buf[100];
					snprintf(buf, 100, "RCD error ABC total: %" PRIu32 " / %.1f  Avg per day: %.1f", errorCountABCTotal, (onTime/86400.0), (errorCountABCTotal/(onTime / 86400.0)));
					publish_debug_telemetry_observation_Diagnostics(buf);
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


static uint8_t waitForCarCountDown = 0;
void sessionHandler_InitiateResetChargeSession()
{
	sessionResetMode = eSESSION_RESET_INITIATED;
	waitForCarCountDown = 5;

	ESP_LOGW(TAG, "ResetSession initiated");
}

void sessionHandler_StopAndResetChargeSession()
{
	///First send STOP command
	if(sessionResetMode == eSESSION_RESET_INITIATED)
	{
		if(GetFinalStopActiveStatus() == 1){
			MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK during stop and reset");
				SetFinalStopActiveStatus(0);
			}
			else
			{
				ESP_LOGE(TAG, "MCU CommandResumeChargingMCU command FAILED during stop and reset");
			}
		}

		SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_PAUSED);
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
		if((MCU_GetChargeMode() != eCAR_CHARGING) || (waitForCarCountDown == 0))
		{
			sessionResetMode = eSESSION_RESET_FINALIZE;
		}

		waitForCarCountDown--;
	}


	else if(sessionResetMode == eSESSION_RESET_FINALIZE)
	{
		ESP_LOGI(TAG, "Transition state START");
		SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_DISCONNECTED);
		sessionResetMode = eSESSION_RESET_DO_RESET;
	}

	else if((sessionResetMode == eSESSION_RESET_DO_RESET) && (chargeSession_HasSessionId() == false))
	{

		MessageType ret = MCU_SendCommandId(CommandResetSession);
		if(ret == MsgCommandAck)
		{
			ESP_LOGW(TAG, "******** MCU ResetSession command OK *********");

			//return 200;
		}
		else
		{
			ESP_LOGE(TAG, "MCU ResetSession command FAILED");
			//return 400;
		}

		//SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_UNINITIALIZED);
		sessionResetMode = eSESSION_RESET_WAIT;
		ESP_LOGI(TAG, "Transition state STOP");
	}

	//Any failed or final state - cleare opModeOverride
	else if((sessionResetMode == eSESSION_RESET_WAIT) && (MCU_GetEnergy() == 0.0))
	{
		ESP_LOGI(TAG, "sessionReset: Energy cleared");
		sessionResetMode = eSESSION_RESET_NONE;
	}
	else if((sessionResetMode == eSESSION_RESET_WAIT) && (MCU_GetEnergy() != 0.0))
	{
		ESP_LOGW(TAG, "sessionReset: MCU_GetEnergy() = %f > 0.0", MCU_GetEnergy());
	}

	if((sessionResetMode == eSESSION_RESET_NONE) )
	{
		SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_UNINITIALIZED);
		ESP_LOGW(TAG, "eSESSION_RESET_NONE");
	}

	ESP_LOGE(TAG, "sessionResetMode: %i cnt %i", sessionResetMode, waitForCarCountDown);
}


void sessionHandler_SendMCUSettings()
{
	char mcuPayload[130];

	ZapMessage rxMsg = MCU_ReadParameter(ParamIsEnabled);
	uint8_t enabled = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(ParamIsStandalone);
	uint8_t standAlone = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(AuthenticationRequired);
	uint8_t auth = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(ParamCurrentInMaximum);
	float maxC = GetFloat(rxMsg.data);

	rxMsg = MCU_ReadParameter(MCUFaultPins);
	uint8_t faultPins = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(ServoState);
	uint8_t servoState = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(ServoMovement);
	uint16_t servoMovement = GetUInt16(rxMsg.data);

	rxMsg = MCU_ReadParameter(RCDTestState);
	uint8_t rcdTestState = rxMsg.data[0];

	rxMsg = MCU_ReadParameter(ParamChargePilotLevelAverage);
	uint16_t averagePilotLevel = GetUInt16(rxMsg.data);

	rxMsg = MCU_ReadParameter(ParamInstantPilotCurrent);
	float instantPilotCurrent = GetFloat(rxMsg.data);

	snprintf(mcuPayload, sizeof(mcuPayload), "MCUSettings: En:%i StA:%i, Auth:%i, MaxC: %2.2f faultPins: 0x%X, SS: %i, SM: %i, RCDts: %i, CM: %i, ADC: %i, CP: %2.2f", enabled, standAlone, auth, maxC, faultPins, servoState, servoMovement, rcdTestState, MCU_GetChargeMode(), averagePilotLevel, instantPilotCurrent);
	ESP_LOGI(TAG, "%s", mcuPayload);
	publish_debug_telemetry_observation_Diagnostics(mcuPayload);
}

void sessionHandler_SendRelayStates()
{
	char mcuPayload[100];

	uint8_t states = MCU_GetRelayStates();

	snprintf(mcuPayload, sizeof(mcuPayload), "RelayStates: %i - PEN: %i, L1: %i", states, ((states >> 1) & 0x01), (states & 0x01));
	ESP_LOGI(TAG, "%s", mcuPayload);
	publish_debug_telemetry_observation_Diagnostics(mcuPayload);
}

void sessionHandler_SendFPGAInfo()
{
	char mcuPayload[130] = {0};

	MCU_GetFPGAInfo(mcuPayload, 130);

	publish_debug_telemetry_observation_Diagnostics(mcuPayload);

}

static uint32_t calibrationId = 0;

void sessionHandler_SendMIDStatus(void) {
	if (MCU_GetMidStoredCalibrationId(&calibrationId) && calibrationId != 0) {
		uint32_t midStatus = 0;
		MCU_GetMidStatus(&midStatus);

		char buf[64];
		snprintf(buf, sizeof (buf), "MID Calibration ID: %" PRIu32 " Status: 0x%08" PRIX32, calibrationId, midStatus);

		publish_debug_telemetry_observation_Diagnostics(buf);
	}
}

static bool hasUpdatedCapability = false;
void sessionHandler_SendMIDStatusUpdate(void) {

	///Once factory calibration is finished, send updated capability observation
	/// so that the calibration status is correct in the portal.
	if((calibration_get_finished_flag() == true) && (hasUpdatedCapability == false))
	{
		hasUpdatedCapability = true;
		publish_debug_telemetry_observation_capabilities();
	}

	if (!calibrationId) {
		return;
	}

	static uint32_t lastMidStatus = 0;
	uint32_t midStatus = 0;

	if (MCU_GetMidStatus(&midStatus) && midStatus != lastMidStatus) {
		char buf[48];
		snprintf(buf, sizeof (buf), "MID Status: 0x%08" PRIX32 " -> 0x%08" PRIX32, lastMidStatus, midStatus);

		publish_debug_telemetry_observation_Diagnostics(buf);

		lastMidStatus = midStatus;
	}
}


/*
 * If we have received an already set SessionId from Cloud while in CHARGE_OPERATION_STATE_CHARGING
 * This indicates that cloud does not have the correct chargeOperatingMode recorded.
*/
void ChargeModeUpdateToCloudNeeded()
{
	if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_CHARGING)
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
	reportChargingStateCommandSent = true;
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

static bool previousPulseOnline = false;
static bool pulseOnline = false;
static bool sendPulseOnChange = false;
void sessionHandler_Pulse()
{
	pulseCounter++;

	if(connectivity_GetMQTTInitialized())
	{
		pulseOnline = isMqttConnected();

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
		if(((pulseInterval != recordedPulseInterval) && pulseOnline) || ((pulseOnline == true) && previousPulseOnline == false))
		{
			ESP_LOGI(TAG,"Sending pulse interval %" PRId32 "", pulseInterval);
			int ret = publish_debug_telemetry_observation_PulseInterval(pulseInterval);

			if(ret == ESP_OK)
			{
				recordedPulseInterval = pulseInterval;
				ESP_LOGI(TAG,"Registered pulse interval");
				pulseSendFailedCounter = 0;
				sendPulseOnChange = true;
			}
			else
			{
				ESP_LOGE(TAG,"Pulse interval send failed");

				//If sending fails, don't continue sending forever -> timeout and set anyway
				pulseSendFailedCounter++;
				if(pulseSendFailedCounter == 10)
				{
					recordedPulseInterval = pulseInterval;
					pulseSendFailedCounter = 0;
					sendPulseOnChange = true;
				}
			}
		}


		/// If going from offline to online - ensure new pulse is sent instantly
		/// Cloud sets charger as online within one minute after new pulse is received.
		//if((pulseOnline == true) && (previousPulseOnline == false))
			//pulseCounter = PULSE_INIT_TIME;

		///Send pulse at interval or when there has been a change in interval
		if(((pulseCounter >= pulseInterval) && (pulseOnline == true)) || ((sendPulseOnChange == true) && (pulseOnline == true)))
		{
			ESP_LOGI(TAG, "PULSE %" PRIi32 "/%" PRIi32 " Change: %i", pulseCounter, pulseInterval, sendPulseOnChange);
			publish_cloud_pulse();

			pulseCounter = 0;
			sendPulseOnChange = false;
		}
	}

	previousPulseOnline = pulseOnline;
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
