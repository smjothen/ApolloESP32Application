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

static const char *TAG = "SESSION    ";

static uint32_t dataTestInterval = 0;
#define RESEND_REQUEST_TIMER_LIMIT 150


static char * completedSessionString = NULL;

TimerHandle_t signedMeterValues_timer;

//Send every 15 minutes - at XX:00, XX:15, XX:30 and XX:45.
void on_send_signed_meter_value()
{
	if(MCU_GetchargeMode() == eCAR_CHARGING)
	{
		ESP_LOGW(TAG, "***** Sending signed meter values *****");

		char OCMPMessage[200] = {0};
		OCMF_CreateNewOCMFMessage(OCMPMessage);

		if (isMqttConnected() == true)
			publish_string_observation(SignedMeterValue, OCMPMessage);

		OCMF_AddElementToOCMFLog("T", "G");
	}
}


void SetDataInterval(int newDataInterval)
{
	dataTestInterval = newDataInterval;
}

static bool authorizationRequired = true;
static bool pendingCloudAuthorization = false;
static char pendingRFIDTag[DEFAULT_STR_SIZE]= {0};
static bool isAuthorized = false;

void SetAuthorized(bool authFromCloud)
{
	isAuthorized = authFromCloud;

	if(isAuthorized == true)
		chargeSession_SetAuthenticationCode(pendingRFIDTag);

	pendingCloudAuthorization = false;
	memset(pendingRFIDTag, 0, DEFAULT_STR_SIZE);
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
void sessionHandler_simulateOffline()
{
	simulateOffline = true;
	simulateOfflineTimeout = 180;
}


static bool offlineCurrentSent = false;
static uint32_t offlineTime = 0;
void OfflineHandler()
{

	int activeSessionId = strlen(chargeSession_GetSessionId());
	uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();

	//Handle charge session started offline
	if((activeSessionId > 0) && (chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING))//2 = Requesting, add definitions
	{

		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "Offline MCU Granted command OK");

			float offlineCurrent = storage_Get_DefaultOfflineCurrent();

			if(storage_Get_NetworkType() == NETWORK_3P3W)
				offlineCurrent = offlineCurrent / 1.732; //sqrt(3) Must give IT3 current like Cloud would do

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

		if(storage_Get_NetworkType() == NETWORK_3P3W)
			offlineCurrent = offlineCurrent / 1.732; //sqrt(3) Must give IT3 current like Cloud would do

		ESP_LOGI(TAG, "Setting offline current to MCU %f", offlineCurrent);

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

void PrintSession()
{
	ESP_LOGW(TAG,"\n SessionId: \t\t%s\n Energy: \t\t%f\n StartDateTime: \t%s\n EndDateTime: \t\t%s\n ReliableClock: \t%i\n StoppedByRFIDUid: \t%i\n AuthenticationCode: \t%s", chargeSession_GetSessionId(), chargeSession_Get().Energy, chargeSession_Get().StartTime, chargeSession_Get().EndTime, chargeSession_Get().ReliableClock, chargeSession_Get().StoppedByRFID, chargeSession_Get().AuthenticationCode);
}


bool startupSent = false;
bool setTimerSyncronization = false;

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

    enum CommunicationMode networkInterface = eCONNECTION_NONE;

    bool isOnline = false;
    bool previousIsOnline = true;
    uint32_t mcuDebugCounter = 0;
    uint32_t previousDebugCounter = 0;
    uint32_t mcuDebugErrorCount = 0;

    // Offline parameters
    uint32_t offlineTime = 0;
    uint32_t secondsSinceLastCheck = 10;
    uint32_t resendRequestTimer = 0;
    uint32_t resendRequestTimerLimit = RESEND_REQUEST_TIMER_LIMIT;
    uint8_t nrOfResendRetries = 0;

    OCMF_Init();
    uint32_t secondsSinceSync = 0;

    TickType_t refresh_ticks = pdMS_TO_TICKS(15*60*1000); //15 minutes
    signedMeterValues_timer = xTimerCreate( "MeterValueTimer", refresh_ticks, pdTRUE, NULL, on_send_signed_meter_value );

	while (1)
	{

		if((!setTimerSyncronization) && zntp_IsSynced())
		{
			if(zntp_Get15MinutePoint())
			{
				ESP_LOGW(TAG, " 15 Min sync!");
				xTimerReset( signedMeterValues_timer, portMAX_DELAY );
				on_send_signed_meter_value();

				setTimerSyncronization = true;
				secondsSinceSync = 0;
			}
		}

		//The timer must be resynced regularly with the clock to avoid deviation since the clock is updated through NTP.
		secondsSinceSync++;
		if((setTimerSyncronization == true) && (secondsSinceSync > 3400) && MCU_GetchargeMode() != eCAR_CHARGING)	//Try to resync in less than an hour (3400 sec)
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

		//If we are charging when going from offline to online, send a stop command to change the state to requesting.
		//This will make the Cloud send a new start command with updated current to take us out of offline current mode
		//Check the offline time to ensure we don't send at every token refresh.
		if(((previousIsOnline == false) && (isOnline == true) && (offlineTime > 120) && (chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING)))
		{
			publish_debug_telemetry_observation_RequestNewStartChargingCommand();
		}


		if(isOnline)
		{
			int sentOk = publish_telemetry_observation_on_change();

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
					}
				}
				else
				{
					ESP_LOGW(TAG, "System mode: Waiting to declare offline: %d", offlineTime);
				}
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
		else if((currentCarChargeMode == eCAR_CONNECTED) && (authorizationRequired == true) && (NFCGetTagInfo().tagIsValid == true) && (chargeSession_Get().SessionId[0] == '\0'))
		{
			ESP_LOGW(TAG, "New session due to tag");
			chargeSession_Start();
			//NFCTagInfoClearValid();
		}

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
					publish_debug_telemetry_observation_NFC_tag_id(NFCGetTagInfo().idAsString);
					publish_debug_telemetry_observation_ChargingStateParameters();
				}

				pendingCloudAuthorization = true;
				strcpy(pendingRFIDTag,NFCGetTagInfo().idAsString);
				//chargeSession_SetAuthenticationCode(NFCGetTagInfo().idAsString);
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
		if(((currentCarChargeMode == eCAR_DISCONNECTED) && (previousCarChargeMode < eCAR_DISCONNECTED)) || (stoppedByRfid == true))
		{
			//Do not send a CompletedSession with no SessionId.
			if(chargeSession_Get().SessionId[0] != '\0')
			{
				OCMF_FinalizeOCMFLog();
				chargeSession_Finalize();
				PrintSession();

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
			//char empty[] = "\0";
			//publish_string_observation(SessionIdentifier, empty);

			chargeSession_Clear();

			NFCClearTag();
		}
		
		//If the FinalStopActive bit is set when a car disconnect, make sure to clear the status value used by Cloud
		if((currentCarChargeMode == eCAR_DISCONNECTED) && (GetFinalStopActiveStatus() == true))
		{
			SetFinalStopActiveStatus(0);
		}

		previousCarChargeMode = currentCarChargeMode;

		onTime++;
		dataCounter++;

		if (onTime > 600)
		{
			if (networkInterface == eCONNECTION_WIFI)
			{
				if ((MCU_GetchargeMode() == 12) || (MCU_GetchargeMode() == 9))
					dataInterval = 3600;	//When car is disconnected or not charging
				else
					dataInterval = 900;	//When car is in charging state

			}
			else if (networkInterface == eCONNECTION_LTE)
			{
				if ((MCU_GetchargeMode() == 12) || (MCU_GetchargeMode() == 9))
					dataInterval = 3600;	//When car is disconnected or not charging
				else
					dataInterval = 900;	//When car is in charging state

				//LTE SignalQuality internal update interval
				signalInterval = 3600;
			}
		}

		//Test-mode overrides default
		if(dataTestInterval != 0)
			dataInterval = dataTestInterval;

		if(dataCounter >= dataInterval)
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
					publish_debug_telemetry_observation_all(rssi);
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
		if(pulseCounter >= 60)
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
				ESP_LOGW(TAG,"******** LTE: %d %%  DataInterval: %d *******", GetCellularQuality(), dataInterval);
			}
			else if (networkInterface == eCONNECTION_WIFI)
			{
				if (esp_wifi_sta_get_ap_info(&wifidata)==0)
					rssi = wifidata.rssi;
				else
					rssi = 0;

				ESP_LOGW(TAG,"******** WIFI: %d dBm  DataInterval: %d  *******", rssi, dataInterval);
			}

			PrintSession();

			statusCounter = 0;
		}


		if (isMqttConnected() == true)
		{
			if (startupSent == false)
			{
				if((networkInterface == eCONNECTION_WIFI))
				{
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
				publish_debug_telemetry_observation_cloud_settings();

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
 * Call this function to resend startup parameters
 */
void ClearStartupSent()
{
	startupSent = false;
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

	completedSessionString = malloc(LOG_STRING_SIZE);
	//Got stack overflow on 5000, try with 6000
	xTaskCreate(sessionHandler_task, "sessionHandler_task", 6000, NULL, 3, &taskSessionHandle);
}
