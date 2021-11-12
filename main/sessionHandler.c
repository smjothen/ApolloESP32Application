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

static const char *TAG = "SESSION    ";

#define RESEND_REQUEST_TIMER_LIMIT 90
#define OCMF_INTERVAL_TIME 3600

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
		ESP_LOGE(TAG, "****** DOUBLE OCMF %d -> RETURNING ******", secSinceLastOCMFMessage);
		return;
	}

	secSinceLastOCMFMessage = 0;

	char OCMPMessage[220] = {0};
	time_t time;
	double energy;

	bool state_charging = MCU_GetchargeMode() == eCAR_CHARGING;
	bool state_log_empty = false;
	int publish_result = -1;

	if((state_charging == true) || (hasCharged == true))
	{
		hasRemainingEnergy = true;
		hasCharged = false;
	}

	if(state_charging || hasRemainingEnergy){
		// sample energy now, dumping the log may be to slow to get the time aligned energy
		OCMF_CreateNewOCMFMessage(OCMPMessage, &time, &energy);
	}

	if(hasRemainingEnergy)
		ESP_LOGW(TAG, "### Set to report any remaining energy. RV=%f ###", energy);

	ESP_LOGI(TAG, "***** Clearing energy log *****");

	if(!isMqttConnected()){
		// do not attempt sending data when we know that the system is offline
	}else if(attempt_log_send()==0){
		ESP_LOGI(TAG, "energy log empty");
		state_log_empty = true;
	}

	if ((state_charging && state_log_empty) || (hasRemainingEnergy && state_log_empty)){
		publish_result = publish_string_observation_blocked(
			SignedMeterValue, OCMPMessage, 10000
		);

		if(publish_result<0){
			append_offline_energy(time, energy);
		}

	}else if(state_charging || hasRemainingEnergy){
		ESP_LOGI(TAG, "failed to empty log, appending new measure");
		append_offline_energy(time, energy);
	}

	if(state_charging || hasRemainingEnergy){
		// add to log late to increase chance of consistent logs across observation types
		OCMF_AddElementToOCMFLog("T", "G");
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
		chargeSession_SaveSessionResetInfo();
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


static uint32_t simulateOfflineTimeout = 180;
static bool simulateOffline = false;
void sessionHandler_simulateOffline(int offlineTime)
{
	simulateOffline = true;
	simulateOfflineTimeout = offlineTime;
}

static bool requestCurrentWhenOnline = false;
static bool offlineCurrentSent = false;
void OfflineHandler()
{

	int activeSessionId = strlen(chargeSession_GetSessionId());
	uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();

	//Handle charge session started offline
	if((activeSessionId > 0) && (chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING))//2 = Requesting, add definitions
	{
		//Wait until a valid tag is registered.
		if((storage_Get_AuthenticationRequired() == 1) && (chargeSession_Get().AuthenticationCode[0] == '\0'))
			return;

		requestCurrentWhenOnline = true;

		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "Offline MCU Granted command OK");

			float offlineCurrent = storage_Get_DefaultOfflineCurrent();

			//Scaling is done in MCU
			//if(MCU_GetGridType() == NETWORK_3P3W)
			//	offlineCurrent = offlineCurrent / 1.732; //sqrt(3) Must give IT3 current like Cloud would do

			MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, offlineCurrent);
			if(ret == MsgWriteAck)
			{
				MessageType ret = MCU_SendCommandId(CommandStartCharging);
				if(ret == MsgCommandAck)
				{
					ESP_LOGI(TAG, "Offline MCU Start command OK: %fA", offlineCurrent);

				}
				else
				{
					ESP_LOGI(TAG, "Offline MCU Start command FAILED");
				}
			}
			else
			{
				ESP_LOGE(TAG, "Offline MCU Start command FAILED");
			}

		}
		else
		{
			ESP_LOGI(TAG, "Offline MCU Granted command FAILED");
		}
	}

	//Handel existing charge session that has gone offline
	//Handle charge session started offline
	else if((activeSessionId > 0) && ((chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) || (chargeOperatingMode == CHARGE_OPERATION_STATE_PAUSED)) && !offlineCurrentSent)//2 = Requesting, add definitions
	{
		float offlineCurrent = storage_Get_DefaultOfflineCurrent();

		//Scaling is done in MCU
		//if(MCU_GetGridType() == NETWORK_3P3W)
		//	offlineCurrent = offlineCurrent / 1.732; //sqrt(3) Must give IT3 current like Cloud would do

		ESP_LOGI(TAG, "Setting offline current to MCU %f", offlineCurrent);

		requestCurrentWhenOnline = true;

		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, offlineCurrent);
		if(ret == MsgWriteAck)
		{
			MessageType ret = MCU_SendCommandId(CommandStartCharging);
			if(ret == MsgCommandAck)
			{
				offlineCurrentSent = true;
				ESP_LOGE(TAG, "Offline MCU Start command OK: %fA", offlineCurrent);
			}
			else
			{
				ESP_LOGE(TAG, "Offline MCU Start command FAILED");
			}
		}
		else
		{
			ESP_LOGE(TAG, "Offline MCU Start command FAILED");
		}
	}
	else if(offlineCurrentSent == true)
	{
		ESP_LOGE(TAG, "Offline current mode. SimulateOfflineTimeout: %d", simulateOfflineTimeout);
	}
}

void sessionHandler_ClearOfflineCurrentSent()
{
	offlineCurrentSent = false;
}


bool startupSent = false;
bool setTimerSyncronization = false;

static bool stoppedByCloud = false;

void sessionHandler_SetStoppedByCloud(bool stateFromCloud)
{
	stoppedByCloud = stateFromCloud;
	SetClearSessionFlag();
}

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

	ESP_LOGW(TAG,"******** FromCloud: %2.1f A, MaxInst: %2.1f A -> Set: %2.1f A, Pilot: %2.1f %%   SetPhases: %d, OfflineCurrent: %2.1f A **************", currentSetFromCloud, storage_Get_MaxInstallationCurrentConfig(), actualCurrentSet, pilot, phasesSetFromCloud, storage_Get_DefaultOfflineCurrent());
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

static void sessionHandler_task()
{
	int8_t rssi = 0;
	wifi_ap_record_t wifidata;
	
	uint32_t onCounter = 0;

	uint32_t onTime = 0;
    uint32_t pulseCounter = 30;

    uint32_t dataCounter = 0;
    uint32_t dataInterval = 120;

    uint32_t statusCounter = 0;
    uint32_t statusInterval = 10;

    uint32_t signalInterval = 120;

    uint32_t signalCounter = 0;

    enum CarChargeMode currentCarChargeMode = eCAR_UNINITIALIZED;
    enum CarChargeMode previousCarChargeMode = eCAR_UNINITIALIZED;

    uint8_t previousChargeOperatingMode = 0;

    enum CommunicationMode networkInterface = eCONNECTION_NONE;

    bool isOnline = false;
    bool previousIsOnline = true;
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

    authentication_Init();
    OCMF_Init();
    uint32_t secondsSinceSync = OCMF_INTERVAL_TIME;

    TickType_t refresh_ticks = pdMS_TO_TICKS(60*60*1000); //60 minutes
    //TickType_t refresh_ticks = pdMS_TO_TICKS(1*60*1000); //1 minutes for testing( also change line in zntp.c for minute sync)
    //TickType_t refresh_ticks = pdMS_TO_TICKS(1*5*1000); //1 minutes for testing( also change line in zntp.c for minute sync)
    signedMeterValues_timer = xTimerCreate( "MeterValueTimer", refresh_ticks, pdTRUE, NULL, on_ocmf_sync_time );

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
		//if((setTimerSyncronization == true) && (secondsSinceSync > 30))// && MCU_GetchargeMode() != eCAR_CHARGING)	//Try to resync in less than an hour (3400 sec)
		{
			ESP_LOGW(TAG, " Trig new OCMF timer sync");
			setTimerSyncronization = false;
		}

		onCounter++;

		//Allow simulating timelimited offline mode initated with cloud command
		if(simulateOffline == true)
		{
			//Override state
			//isOnline = false;
			MqttSetSimulatedOffline(true);

			simulateOfflineTimeout--;
			if(simulateOfflineTimeout == 0)
			{
				simulateOffline= false;
				MqttSetSimulatedOffline(false);
			}
		}

		isOnline = isMqttConnected();

		//Always ensure offlineCurrentSent is ready to be sent to MCU in case we go offline
		if(isOnline == true)
		{
			offlineCurrentSent = false;
			offlineTime = 0;
		}
		else
		{
			offlineTime++;
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

				vTaskDelay(5000 / portTICK_PERIOD_MS);

				esp_restart();
			}
		}

		if(networkInterface == eCONNECTION_NONE)
		{
			if((onCounter % 10) == 0)
				ESP_LOGI(TAG, "No connection configured");

			//vTaskDelay(pdMS_TO_TICKS(1000));
			//continue;
		}
		else if(!isOnline)
		{
			if((onCounter % 10) == 0)
			{
				if(storage_Get_Standalone() == 1)
					ESP_LOGI(TAG, "Offline - Standalone");
				else
					ESP_LOGI(TAG, "Offline - System");
			}
		}

		if(chargeSession_HasNewSessionId() == true)
		{
			int ret = publish_string_observation(SessionIdentifier, chargeSession_GetSessionId());
			if(ret == 0)
				chargeSession_ClearHasNewSession();
		}

		uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();
		currentCarChargeMode = MCU_GetchargeMode();

		//We need to inform the ChargeSession if a car is connected.
		//If car is disconnected just before a new sessionId is received, the sessionId should be rejected
		if((currentCarChargeMode == eCAR_DISCONNECTED) || (currentCarChargeMode == eCAR_UNINITIALIZED))
			SetCarConnectedState(false);
		else
			SetCarConnectedState(true);

		//If we are charging when going from offline to online, send a stop command to change the state to requesting.
		//This will make the Cloud send a new start command with updated current to take us out of offline current mode
		//Check the requestCurrentWhenOnline to ensure we don't send at every token refresh.
		if((previousIsOnline == false) && (isOnline == true) && (chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) && requestCurrentWhenOnline)
		{
			publish_debug_telemetry_observation_RequestNewStartChargingCommand();
			requestCurrentWhenOnline = false;
		}


		if(isOnline)
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
				ESP_LOGE(TAG, "CHARGE STATE resendTimer: %d/%d", resendRequestTimer, resendRequestTimerLimit);
				if(resendRequestTimer >= resendRequestTimerLimit)
				{
					publish_debug_telemetry_observation_ChargingStateParameters();

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
				//if(resendRequestTimerLimit != RESEND_REQUEST_TIMER_LIMIT)
					//ESP_LOGE(TAG, "CHARGE STATE timer reset!");

				resendRequestTimer = 0;
				resendRequestTimerLimit = RESEND_REQUEST_TIMER_LIMIT;
				nrOfResendRetries = 0;
			}


			offlineTime = 0;
			offlineMode = false;
		}
		else
		{
			if(storage_Get_Standalone() == false)
			{
				offlineTime++;
				if(offlineTime > 120)
				{
					if(secondsSinceLastCheck < 10)
					{
						secondsSinceLastCheck++;
					}
					if(secondsSinceLastCheck >= 10)
					{
						OfflineHandler();
						secondsSinceLastCheck = 0;
						offlineMode = true;
					}
				}
				else
				{
					ESP_LOGW(TAG, "System mode: Waiting to declare offline: %d", offlineTime);
				}
			}
			else
			{
				//OfflineMode is only for System use
				offlineMode = false;
			}
		}


		if((previousCarChargeMode == eCAR_UNINITIALIZED) && (currentCarChargeMode == eCAR_DISCONNECTED))
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
		}

		// Check if car connecting -> start a new session
		if((currentCarChargeMode < eCAR_DISCONNECTED) && (previousCarChargeMode >= eCAR_DISCONNECTED))
		{
			chargeSession_Start();
		}
		else if((currentCarChargeMode < eCAR_DISCONNECTED) && (sessionIDClearedByCloud == true))
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

		if((currentCarChargeMode < eCAR_DISCONNECTED) && (authorizationRequired == true))
		{

			if((NFCGetTagInfo().tagIsValid == true) && (stoppedByRfid == false))
			{
//				int i = 0;
//				for (i = 0; i < NFCGetTagInfo().idLength; i++)
//				{
//					sprintf(NFCGetTagInfo().idAsString+(i*2),"%02X ", NFCGetTagInfo().id[i] );
//				}

				if (isMqttConnected() == true)
				{
					MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
					if(ret == MsgWriteAck)
						ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
					else
						ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

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
						chargeSession_SaveSessionResetInfo();
					}
				}

				NFCTagInfoClearValid();

			}
			//De-authorize in cloud?
			else if(stoppedByRfid == true)
			{
				if((strcmp(chargeSession_Get().AuthenticationCode, NFCGetTagInfo().idAsString) == 0))
				{
					if (isMqttConnected() == true)
					{
						publish_debug_telemetry_observation_NFC_tag_id(NULL);
						publish_debug_telemetry_observation_ChargingStateParameters();
					}
					//NFCTagInfoClearValid();
				}
			}
		}


		if(currentCarChargeMode < eCAR_DISCONNECTED)
			chargeSession_UpdateEnergy();

		// Check if car connecting -> start a new session
		if(((currentCarChargeMode == eCAR_DISCONNECTED) && (previousCarChargeMode < eCAR_DISCONNECTED)) || (stoppedByRfid == true) || (stoppedByCloud == true))
		{
			//Do not send a CompletedSession with no SessionId.
			if(chargeSession_Get().SessionId[0] != '\0')
			{
				OCMF_FinalizeOCMFLog();
				chargeSession_Finalize();
				chargeSession_PrintSession();

				//char completedSessionString[200] = {0};
				memset(completedSessionString,0, LOG_STRING_SIZE);
				chargeSession_GetSessionAsString(completedSessionString);

				// Delay to space data recorded i cloud.
				//vTaskDelay(pdMS_TO_TICKS(2000));

				if (isMqttConnected() == true)
				{
					int i;
					for (i = 1; i <= 3; i++)
					{
						//Try sending 3 times. This transmission has been made a blocking call
						int ret = publish_debug_telemetry_observation_CompletedSession(completedSessionString);
						if (ret == 0)
							break;
						else
							ESP_LOGE(TAG," CompletedSession failed %i/3", i);
					}
				}

			}

			//Reset if set
			if(stoppedByCloud == true)
			{
				stoppedByCloud = false;
				sessionIDClearedByCloud = true;
				ESP_LOGE(TAG," Session ended by Cloud");

				//char empty[] = "\0";
				publish_string_observation(SessionIdentifier, NULL);
			}
			chargeSession_Clear();

			NFCClearTag();

			//Ensure the authentication status is cleared at disconnect
			i2cClearAuthentication();
		}
		
		//If the FinalStopActive bit is set when a car disconnect, make sure to clear the status value used by Cloud
		if((currentCarChargeMode == eCAR_DISCONNECTED) && (GetFinalStopActiveStatus() == true))
		{
			SetFinalStopActiveStatus(0);
		}

		//If session is cleared while car is disconnecting, ensure a new session is not generated incorrectly
		if(currentCarChargeMode == eCAR_DISCONNECTED)
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


		previousCarChargeMode = currentCarChargeMode;
		previousChargeOperatingMode = chargeOperatingMode;

		onTime++;
		dataCounter++;

		if (onTime > 600)
		{
			if (networkInterface == eCONNECTION_WIFI)
			{
				if ((MCU_GetchargeMode() == 12) || (MCU_GetchargeMode() == 9))
					dataInterval = storage_Get_TransmitInterval();//3600;	//When car is disconnected or not charging
				else
					dataInterval = 900;	//When car is in charging state

			}
			else if (networkInterface == eCONNECTION_LTE)
			{
				if ((MCU_GetchargeMode() == 12) || (MCU_GetchargeMode() == 9))
					dataInterval = storage_Get_TransmitInterval();//3600;	//When car is disconnected or not charging
				else
					dataInterval = 900;	//When car is in charging state

				//LTE SignalQuality internal update interval
				signalInterval = 3600;
			}
		}

		//Test-mode overrides default
		//if(dataTestInterval != 0)
		//	dataInterval = dataTestInterval;

		if((dataCounter >= dataInterval) && (storage_Get_TransmitInterval() > 0))
		{

			if (isMqttConnected() == true)
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
			if((signalCounter >= signalInterval) && (otaIsRunning() == false))
			{
				if (isMqttConnected() == true)
				{
					//log_task_info();
					log_cellular_quality(); // check if OTA is in progress before calling this
				}

				signalCounter = 0;
			}
		}

		pulseCounter++;
		if((pulseCounter >= storage_Get_PulseInterval()) || (!previousIsOnline && isOnline))
		{
			if (isMqttConnected() == true)
			{
				publish_cloud_pulse();
			}

			pulseCounter = 0;
		}

		statusCounter++;
		if(statusCounter >= statusInterval)
		{

			if (networkInterface == eCONNECTION_LTE)
			{
				ESP_LOGW(TAG,"******** LTE: %d %%  DataInterval: %d  Pulse: %d *******", GetCellularQuality(), dataInterval, storage_Get_PulseInterval());
			}
			else if (networkInterface == eCONNECTION_WIFI)
			{
				if (esp_wifi_sta_get_ap_info(&wifidata)==0)
					rssi = wifidata.rssi;
				else
					rssi = 0;

				ESP_LOGW(TAG,"******** WIFI: %d dBm  DataInterval: %d  Pulse: %d *******", rssi, dataInterval, storage_Get_PulseInterval());
			}

			//This is to make cloud settings visible during developement
			if(storage_Get_Standalone() == false)
			{
				sessionHandler_PrintParametersFromCloud();
				if((MCU_GetchargeMode() == 12))
				{
					//Clear if car is disconnected
					currentSetFromCloud = 0.0;
					phasesSetFromCloud = 0;
				}
			}

			chargeSession_PrintSession();

			statusCounter = 0;
		}


		if (isMqttConnected() == true)
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
					int published = publish_debug_telemetry_observation_Diagnostics(gtr);
					free(gtr);

					if (published == 0)
					{
						ClearMCUDiagnosicsResults();
						ESP_LOGW(TAG,"Diagnostics flag cleared");
					}
					else
					{
						ESP_LOGE(TAG,"Diagnostics flag NOT cleared");
					}
				}
				else
				{
					ESP_LOGW(TAG,"Diagnostics length = 0");
					ClearMCUDiagnosicsResults();
				}
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
				int published = publish_debug_telemetry_observation_InstallationConfigOnFile();

				if (published == 0)
				{
					ClearReportGridTestResults();
				}
			}


			if(HasNewData() == true)
			{
				int published = publish_diagnostics_observation(GetATBuffer());

				if (published == 0)
				{
					ClearATBuffer();
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

		previousIsOnline = isOnline;

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

/*
 * If we have received an already set SesssionId from Cloud while in CHARGE_OPERATION_STATE_CHARGING
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

void sessionHandler_init(){

	ocmf_sync_semaphore = xSemaphoreCreateBinary();
	xTaskCreate(ocmf_sync_task, "ocmf", 5000, NULL, 3, &taskSessionHandleOCMF);

	completedSessionString = malloc(LOG_STRING_SIZE);
	//Got stack overflow on 5000, try with 6000
	xTaskCreate(sessionHandler_task, "sessionHandler_task", 6000, NULL, 3, &taskSessionHandle);

}
