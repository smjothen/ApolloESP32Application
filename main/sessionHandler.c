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
#include "chargeController.h"

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

static enum  SessionResetMode sessionResetMode = eSESSION_RESET_NONE;

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

    uint32_t previousWarnings = 0;
    bool firstTimeAfterBoot = true;
    uint8_t countdown = 5;

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
				ESP_LOGE(TAG, "ESP resetting due to mcuDebugCounter: %i", mcuDebugCounter);
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
					ESP_LOGI(TAG, "System mode: Waiting to declare offline: %d/%d", offlineTime, recordedPulseInterval * 2);
				}
			}
			else
			{
				//OfflineMode is only for System use
				offlineMode = false;
			}
		}


		// Check if car connecting -> start a new session
		if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (previousChargeOperatingMode <= CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			chargeSession_Start();
		}

		bool stoppedByRfid = chargeSession_Get().StoppedByRFID;

		if((chargeOperatingMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (authorizationRequired == true))
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

				if(chargeController_IsScheduleActive())
					publish_debug_telemetry_observation_TimeAndSchedule(0x7);


				sessionHandler_SendFPGAInfo();

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
			ESP_LOGI(TAG, "MCU ResetSession command OK");

			//return 200;
		}
		else
		{
			ESP_LOGE(TAG, "MCU ResetSession command FAILED");
			//return 400;
		}

		SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_UNINITIALIZED);
		sessionResetMode = eSESSION_RESET_NONE;
		ESP_LOGI(TAG, "Transition state STOP");
	}

	//Any failed or final state - cleare opModeOverride
	if(sessionResetMode == eSESSION_RESET_NONE)
	{
		SetTransitionOperatingModeState(CHARGE_OPERATION_STATE_UNINITIALIZED);
	}

	ESP_LOGE(TAG, "sessionResetMode: %i cnt %i", sessionResetMode, waitForCarCountDown);
}


void sessionHandler_SendMCUSettings()
{
	char mcuPayload[100];

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

	snprintf(mcuPayload, sizeof(mcuPayload), "MCUSettings: En:%i StA:%i, Auth:%i, MaxC: %2.2f faultPins: 0x%X", enabled, standAlone, auth, maxC, faultPins);
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
			ESP_LOGI(TAG,"Sending pulse interval %d", pulseInterval);
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
			ESP_LOGI(TAG, "PULSE %i/%i Change: %i", pulseCounter, pulseInterval, sendPulseOnChange);
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
