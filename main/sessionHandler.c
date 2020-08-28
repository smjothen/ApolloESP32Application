#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "sessionHandler.h"
#include "CLRC661.h"
#include "ppp_task.h"
#include "at_commands.h"
#include "zaptec_cloud_observations.h"
#include "connect.h"
#include "protocol_task.h"
#include "zaptec_cloud_listener.h"
#include "DeviceInfo.h"

#define NO_OF_SAMPLES   1//10          

static const char *TAG = "SESSION    ";

//#define USE_CELLULAR_CONNECTION 1

//uint32_t template = 0;

bool authorizationRequired = true;
static int rssi2 = 0;

void log_cellular_quality(void){
	#ifndef USE_CELLULAR_CONNECTION
	return;
	#endif
	int enter_command_mode_result = enter_command_mode();

	if(enter_command_mode_result<0){
		ESP_LOGW(TAG, "failed to enter command mode, skiping rssi log");
		vTaskDelay(pdMS_TO_TICKS(500));// wait to make sure all logs are flushed
		return;
	}

	char sysmode[16]; int rssi; int rsrp; int sinr; int rsrq;
	at_command_signal_strength(sysmode, &rssi, &rsrp, &sinr, &rsrq);

	char signal_string[256];
	snprintf(signal_string, 256, "[AT+QCSQ Report Signal Strength] mode: %s, rssi: %d, rsrp: %d, sinr: %d, rsrq: %d", sysmode, rssi, rsrp, sinr, rsrq);
	ESP_LOGI(TAG, "sending diagnostics observation (1/2): \"%s\"", signal_string);
	publish_diagnostics_observation(signal_string);

	//int rssi2;
	int ber;
	char quality_string[256];
	at_command_signal_quality(&rssi2, &ber);
	snprintf(quality_string, 256, "[AT+CSQ Signal Quality Report] rssi: %d, ber: %d", rssi2, ber);
	ESP_LOGI(TAG, "sending diagnostics observation (2/2): \"%s\"", quality_string );
	publish_diagnostics_observation(quality_string);

	int enter_data_mode_result = enter_data_mode();
	ESP_LOGI(TAG, "at command poll:[%d];[%d];", enter_command_mode_result, enter_data_mode_result);


	// publish_debug_telemetry_observation(221.0, 222, 0.0, 1.0,2.0,3.0, 23.0, 42.0);
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



static void sessionHandler_task()
{
	int8_t rssi = 0;
	wifi_ap_record_t wifidata;
	
	uint32_t onTime = 0;
    uint32_t pulseCounter = 30;

    uint32_t dataCounter = 50;
    uint32_t dataInterval = 60;

    uint32_t statusCounter = 0;
    uint32_t statusInterval = 10;

    uint32_t signalInterval = 120;

#ifdef USE_CELLULAR_CONNECTION
    uint32_t signalCounter = 0;
    log_task_info();
    log_cellular_quality();
#endif


    //Send at startup
    publish_debug_telemetry_observation_StartUpParameters();


	while (1) {

		//ESP_LOGI(TAG, "Raw6: %d\tVoltage6: %.2fV \t Raw0: %d\tVoltage0: %.2fV \t %d%%", adc_reading6, voltage6, adc_reading0, voltage0, percentage0);
		if(authorizationRequired == true)
		{

			if(NFCGetTagInfo().tagIsValid == true)
			{
				char NFCHexString[11];
				int i = 0;
				for (i = 0; i < NFCGetTagInfo().idLength; i++)
					sprintf(NFCHexString+(i*2),"%02X ", NFCGetTagInfo().id[i] );

				publish_debug_telemetry_observation_NFC_tag_id(NFCHexString);

				NFCClearTag();
			}
		}

		//ESP_LOGI(TAG, "SessionHandler");
		

		onTime++;
		dataCounter++;

		if (onTime > 600)
		{
			if (MCU_GetchargeMode() != 12)
				dataInterval = 60;
			else
				dataInterval = 600;

			signalInterval = 300;
		}


		if(dataCounter >= dataInterval)
		{
			if (esp_wifi_sta_get_ap_info(&wifidata)==0){
				rssi = wifidata.rssi;
			}
			else
				rssi = 0;

		#ifdef USE_CELLULAR_CONNECTION
			rssi = rssi2;
		#endif


			//mqtt_reconnect();

			if((WifiIsConnected() == true) || (LteIsConnected() == true))
			{
				//publish_debug_telemetry_observation(221.0, 222, 0.0, 1.0,2.0,3.0, temperature, 42.0);
				//publish_debug_telemetry_observation(temperature, 0.0, rssi);
				//publish_debug_telemetry_observation_power(MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2), MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2));
				publish_debug_telemetry_observation_all(MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2), MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1), MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2), MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2), rssi);
			}
			else
			{
				ESP_LOGE(TAG, "No network DISCONNECTED");
			}

			//mqtt_disconnect();

			dataCounter = 0;

		}


	#ifdef USE_CELLULAR_CONNECTION
		signalCounter++;
		if(signalCounter >= signalInterval)
		{
			if((WifiIsConnected() == true) || (LteIsConnected() == true))
			{
				//log_task_info();
				log_cellular_quality();
			}

			signalCounter = 0;
		}
	#endif

		pulseCounter++;
		if(pulseCounter >= 60)
		{
			if((WifiIsConnected() == true) || (LteIsConnected() == true))
			{
				publish_cloud_pulse();
			}

			pulseCounter = 0;
		}


		statusCounter++;
		if(statusCounter >= statusInterval)
		{



			//rssi = rssi2;

			//size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

#ifdef USE_CELLULAR_CONNECTION
			int dBm = 2*rssi2 - 113;
			ESP_LOGW(TAG,"******** Ind %d: %d dBm  DataInterval: %d *******", rssi2, dBm, dataInterval);
#else
			if (esp_wifi_sta_get_ap_info(&wifidata)==0){
				rssi = wifidata.rssi;
			}
			else
				rssi = 0;

			ESP_LOGW(TAG,"********  %d dBm  DataInterval: %d *******", rssi, dataInterval);
#endif
			statusCounter = 0;
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}



uint32_t GetTemplate()
{
	return 0;//template;
}

void sessionHandler_init(){

	xTaskCreate(sessionHandler_task, "sessionHandler_task", 4096, NULL, 3, NULL);
}
