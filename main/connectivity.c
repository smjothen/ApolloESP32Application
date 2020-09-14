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

static const char *TAG = "CONNECTIVITY: ";


enum ConnectionInterface activeInterface = eCONNECTION_NO_INTERFACE;
static enum ConnectionInterface staticNewInterface = eCONNECTION_NO_INTERFACE;
static enum ConnectionInterface previousInterface = eCONNECTION_NO_INTERFACE;

void connectivityActivateInterface(enum ConnectionInterface selectedInterface)
{
	staticNewInterface = selectedInterface;
}

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
	//activeInterface
	staticNewInterface = (enum ConnectionInterface)Storage_Get_CommunicationMode();

	enum ConnectionInterface localNewInterface = eCONNECTION_NO_INTERFACE;

	bool interfaceChange = false;


	while (1) {

		// Use local variable to avoid value updating from other thread during loop.
		localNewInterface = staticNewInterface;

		if(activeInterface != localNewInterface)
			interfaceChange = true;

		// If an interface is active and there is a change
		if(interfaceChange == true)		//  (localActiveInterface != eCONNECTION_NO_INTERFACE) && (localActiveInterface != previousInterface))
		{
			if(activeInterface == eCONNECTION_NO_INTERFACE)
			{
				ESP_LOGI(TAG, "Nothing to deinit, ready to init new interface");
			}
			else if(activeInterface == eCONNECTION_WIFI)
			{
				ESP_LOGI(TAG, "Deinit Wifi interface");
				// Stop mqtt
				stop_cloud_listener_task();

				// Disconnect wifi
				network_disconnect_wifi();

				vTaskDelay(pdMS_TO_TICKS(3000));
			}
			else if(activeInterface == eCONNECTION_4G)
			{
				ESP_LOGI(TAG, "Deinit 4G interface");
			}
		}


		if(interfaceChange == true)
		{


			if(localNewInterface == eCONNECTION_NO_INTERFACE)
			{
				ESP_LOGI(TAG, "No network interface selected");
			}
			else if(localNewInterface == eCONNECTION_WIFI)
			{
				ESP_LOGI(TAG, "Wifi interface activating");
				network_connect_wifi();
				interfaceChange = false;
			}
			else if(localNewInterface == eCONNECTION_4G)
			{
				ESP_LOGI(TAG, "4G interface activating");

			}
		}

		previousInterface = activeInterface;
		activeInterface = localNewInterface;

		ESP_LOGW(TAG, "**** Connectivity ****");
		vTaskDelay(pdMS_TO_TICKS(3000));
	}

}


void connectivity_init(){


	xTaskCreate(connectivity_task, "connectivity_task", 4096, NULL, 2, NULL);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}
