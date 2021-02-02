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

static const char *TAG = "SESSION    ";

static uint32_t dataTestInterval = 0;


void SetDataInterval(int newDataInterval)
{
	dataTestInterval = newDataInterval;
}

bool authorizationRequired = true;


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


static void sessionHandler_task()
{
	int8_t rssi = 0;
	wifi_ap_record_t wifidata;
	
	uint32_t onTime = 0;
    uint32_t pulseCounter = 30;

    uint32_t dataCounter = 50;
    uint32_t dataInterval = 120;//60;

    uint32_t statusCounter = 0;
    uint32_t statusInterval = 10;

    uint32_t signalInterval = 120;

    uint32_t signalCounter = 0;
    bool startupSent = false;

    enum CarChargeMode currentCarChargeMode = eCAR_UNINITIALIZED;
    enum CarChargeMode previousCarChargeMode = eCAR_UNINITIALIZED;

    enum CommunicationMode networkInterface = eCONNECTION_NONE;

    bool isOnline = false;

	while (1)
	{
		isOnline = isMqttConnected();
		networkInterface = connectivity_GetActivateInterface();

		if((!isOnline) || (networkInterface == eCONNECTION_NONE)) // Also the case if CommunicationMode == eNONE.
		{
			ESP_LOGI(TAG, "Waiting to become online...");
			vTaskDelay(pdMS_TO_TICKS(2000));
			continue;
		}


		if(chargeSession_HasNewSessionId() == true)
		{
			int ret = publish_string_observation(SessionIdentifier, chargeSession_GetSessionId());
			if(ret == 0)
				chargeSession_ClearHasNewSession();
		}

		currentCarChargeMode = MCU_GetchargeMode();

		publish_telemetry_observation_on_change();

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
				chargeSession_Start();

		if((currentCarChargeMode < eCAR_DISCONNECTED) && (authorizationRequired == true))
		{

			if(NFCGetTagInfo().tagIsValid == true)
			{
//				int i = 0;
//				for (i = 0; i < NFCGetTagInfo().idLength; i++)
//				{
//					sprintf(NFCGetTagInfo().idAsString+(i*2),"%02X ", NFCGetTagInfo().id[i] );
//				}

				if (isMqttConnected() == true)
				{
					publish_debug_telemetry_observation_NFC_tag_id(NFCGetTagInfo().idAsString);

				}

				chargeSession_SetAuthenticationCode(NFCGetTagInfo().idAsString);
				NFCTagInfoClearValid();

			}
		}


		if(currentCarChargeMode < eCAR_DISCONNECTED)
			chargeSession_UpdateEnergy();

		// Check if car connecting -> start a new session
		if((currentCarChargeMode == eCAR_DISCONNECTED) && (previousCarChargeMode < eCAR_DISCONNECTED))
		{
			chargeSession_Finalize();
			char completedSessionString[200] = {0};
			chargeSession_GetSessionAsString(completedSessionString);

			// Delay to space data recorded i cloud.
			vTaskDelay(pdMS_TO_TICKS(2000));

			publish_debug_telemetry_observation_CompletedSession(completedSessionString);

			//char empty[] = "\0";
			//publish_string_observation(SessionIdentifier, empty);

			chargeSession_Clear();

			NFCClearTag();
		}
		
		previousCarChargeMode = currentCarChargeMode;

		onTime++;
		dataCounter++;

		if (onTime > 600)
		{
			if (networkInterface == eCONNECTION_WIFI)
			{
				if (MCU_GetchargeMode() == 12)
					dataInterval = 600;	//When car is disconnected
				else
					dataInterval = 60;	//When car connected

			}
			else if (networkInterface == eCONNECTION_LTE)
			{
				if (MCU_GetchargeMode() == 12)
					dataInterval = 1800;	//When car is disconnected
				else
					dataInterval = 600;	//When car connected

				//LTE SignalQuality internal update interval
				signalInterval = 1800;
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

				publish_debug_telemetry_observation_all(MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2), MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1), MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2), MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2), rssi);
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
		//if(pulseCounter >= 60)
		if(pulseCounter >= 90)
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
				ESP_LOGW(TAG,"******** LTE: %d dBm  DataInterval: %d  -  Sid: %s, Uid: %s *******", GetCellularQuality(), dataInterval, chargeSession_GetSessionId(), chargeSession_Get().AuthenticationCode);
			}
			else if (networkInterface == eCONNECTION_WIFI)
			{
				if (esp_wifi_sta_get_ap_info(&wifidata)==0)
					rssi = wifidata.rssi;
				else
					rssi = 0;

				ESP_LOGW(TAG,"******** WIFI: %d dBm  DataInterval: %d  -  Sid: %s, Uid: %s *******", rssi, dataInterval, chargeSession_GetSessionId(), chargeSession_Get().AuthenticationCode);
			}

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
						log_cellular_quality();

					publish_debug_telemetry_observation_LteParameters();
				}

				publish_debug_telemetry_observation_StartUpParameters();

				startupSent = true;
			}


			if(CloudSettingsAreUpdated() == true)
			{
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

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
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

	xTaskCreate(sessionHandler_task, "sessionHandler_task", 4096, NULL, 3, &taskSessionHandle);
}
