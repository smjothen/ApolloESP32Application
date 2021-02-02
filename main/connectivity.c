#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "connectivity.h"
#include "network.h"
#include "DeviceInfo.h"
#include "storage.h"
#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"
#include "../components/ntp/zntp.h"
#include "i2cDevices.h"
#include "../components/cellular_modem/include/ppp_task.h"
#include "sessionHandler.h"

static const char *TAG = "CONNECTIVITY: ";


enum CommunicationMode activeInterface = eCONNECTION_NONE;
static enum CommunicationMode staticNewInterface = eCONNECTION_NONE;
static enum CommunicationMode previousInterface = eCONNECTION_NONE;

static bool sntpInitialized = false;
static bool mqttInitialized = false;


bool connectivity_GetSNTPInitialized()
{
	return sntpInitialized;
}

void connectivity_ActivateInterface(enum CommunicationMode selectedInterface)
{
	staticNewInterface = selectedInterface;
}

enum CommunicationMode connectivity_GetActivateInterface()
{
	return activeInterface;
}

bool wifiInitialized = false;

/*
 * This task shall handle the initiation and switching between wireless
 * interfaces and protocols:
 * -BLE
 * -WiFi
 * -4G
 *
 * -MQTT
 * -(OCPP in future versions)
 */
static void connectivity_task()
{
	//Read from Flash. If no interface is configured, use none and wait for setting
	staticNewInterface = (enum CommunicationMode)storage_Get_CommunicationMode();

	enum CommunicationMode localNewInterface = eCONNECTION_NONE;

	bool interfaceChange = false;
	bool zntpIsRunning = false;

	while (1) {

		// Use local variable to avoid value updating from other thread during loop.
		localNewInterface = staticNewInterface;

		if(activeInterface != localNewInterface)
			interfaceChange = true;

		// If an interface is active and there is a change
		if(interfaceChange == true)		//  (localActiveInterface != eCONNECTION_NO_INTERFACE) && (localActiveInterface != previousInterface))
		{
			if((activeInterface == eCONNECTION_NONE) && (wifiInitialized == false))
			{
				ESP_LOGI(TAG, "Nothing to deinit, ready to init new interface");
			}
			else if((activeInterface == eCONNECTION_WIFI) || (wifiInitialized == true))
			{
				ESP_LOGI(TAG, "Deinit Wifi interface");
				// Stop mqtt
				stop_cloud_listener_task();

				// Disconnect wifi
				network_disconnect_wifi();

				// Reset connectivity status
				//sntpInitialized = false;
				mqttInitialized = false;

				wifiInitialized = false;

				vTaskDelay(pdMS_TO_TICKS(10000));
			}
			else if(activeInterface == eCONNECTION_LTE)
			{
				ESP_LOGI(TAG, "Deinit LTE interface");

				// Stop mqtt
				stop_cloud_listener_task();

				ppp_disconnect();

				// Reset connectivity status
				//sntpInitialized = false;
				mqttInitialized = false;

				wifiInitialized = false;

				vTaskDelay(pdMS_TO_TICKS(10000));
			}
		/*}


		if(interfaceChange == true)
		{*/

			if(localNewInterface == eCONNECTION_NONE)
			{
				ESP_LOGI(TAG, "No network interface selected");
			}
			else if(localNewInterface == eCONNECTION_WIFI)
			{
				ESP_LOGI(TAG, "Wifi interface activating");
				network_connect_wifi(false);
				wifiInitialized = true;
				if(network_WifiIsConnected() == false)
					interfaceChange = true;
				else
					interfaceChange = false;

				//activeInterface = localNewInterface;
			}
			else if(localNewInterface == eCONNECTION_LTE)
			{
				ESP_LOGI(TAG, "LTE interface activating");
				configure_uart(921600);
				ppp_task_start();
				//start_cloud_listener_task(i2cGetLoadedDeviceInfo());
				interfaceChange = false;

				//activeInterface = localNewInterface;
			}
		}

		//if(interfaceChange == false)
		//{
		previousInterface = activeInterface;
		activeInterface = localNewInterface;
		//}

		//Handle SNTP connection if we are online either with Wifi or 4G.
		if((network_WifiIsConnected() == true) || (LteIsConnected() == true))
		{
			if((sntpInitialized == false) && (i2cRTCChecked() == true))
			{
				ESP_LOGW(TAG, "Initializing SNTP after first network connection");
				zntp_init();
				zntp_checkSyncStatus();

				zntpIsRunning = true;
				sntpInitialized = true;
			}
			else if (zntpIsRunning == false)
			{
				ESP_LOGW(TAG, "Restarting SNTP after network reconnection");
				zntp_restart();
				zntpIsRunning = true;
			}
		}
		else if(zntp_enabled() == 1)
		{
			ESP_LOGW(TAG, "Stopping SNTP after network disconnection");
			zntp_stop();
			zntpIsRunning = false;
		}


		//Activate MQTT when we are online and has NTP time or RTC time is good.
		if((sntpInitialized == true) && (mqttInitialized == false) && (localNewInterface != eCONNECTION_NONE))
		{
			//Make sure Device info has been read from EEPROM before connecting to cloud.
			if(i2CDeviceInfoIsLoaded() == true)
			{
				start_cloud_listener_task(i2cGetLoadedDeviceInfo());
				mqttInitialized = true;
			}
		}


		cloud_listener_check_cmd();


		//ESP_LOGI(TAG, "**** Connectivity ****");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

}


static TaskHandle_t taskConnHandle = NULL;
int connectivity_GetStackWatermark()
{
	if(taskConnHandle != NULL)
		return uxTaskGetStackHighWaterMark(taskConnHandle);
	else
		return -1;
}

void connectivity_init()
{
	xTaskCreate(connectivity_task, "connectivity_task", 8192, NULL, 2, &taskConnHandle);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}
