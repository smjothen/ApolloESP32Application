#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"

#include "esp_websocket_client.h"
#include "protocol_task.h"
#include "ppp_task.h"
#include "../components/adc/adc_control.h"
#include "network.h"
#include "i2cDevices.h"
#include "DeviceInfo.h"
#include "sessionHandler.h"
#include "production_test.h"
#include "EEPROM.h"
#include "storage.h"
#include "diagnostics_port.h"
#include "../components/ble/ble_interface.h"
#include "connectivity.h"
#include "apollo_ota.h"

static const char *TAG = "MAIN     ";

//OUTPUT PIN
#define GPIO_OUTPUT_DEBUG_LED    0
#define GPIO_OUTPUT_DEBUG_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED)

char softwareVersion[] = "0.0.0.2";
char softwareVersionBLEtemp[] = "2.8.0.2";	//USED to face ble version

uint8_t GetEEPROMFormatVersion()
{
	return 1;
}

char * GetSoftwareVersion()
{
	return softwareVersion;
}

char * GetSoftwareVersionBLE()
{
	return softwareVersionBLEtemp;
}


void InitGPIOs()
{
    gpio_config_t output_conf;
	output_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	output_conf.mode = GPIO_MODE_OUTPUT;
	output_conf.pin_bit_mask = GPIO_OUTPUT_DEBUG_PIN_SEL;
	output_conf.pull_down_en = 0;
	output_conf.pull_up_en = 0;
	gpio_config(&output_conf);
}


void app_main(void)
{
	//First check hardware revision in order to configure io accordingly
	adc_init();

	InitGPIOs();

	ESP_LOGE(TAG, "Apollo multi-mode");

	storage_Init();

	//Init to read device ID from EEPROM
	I2CDevicesInit();

    zaptecProtocolStart();

    start_ota_task();

    vTaskDelay(pdMS_TO_TICKS(3000));


#define DEV
#ifdef DEV
    int switchState = MCU_GetSwitchState();

    while(switchState == 0)
    {
    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    	switchState = MCU_GetSwitchState();
    }

    if (switchState <= eConfig_Wifi_EMC_TCP)
    {
		char WifiSSID[32]= {0};
		char WifiPSK[64] = {0};

		if(switchState == eConfig_Wifi_NVS)
		{
			network_CheckWifiParameters();
		}
		else
		{
			if(switchState == eConfig_Wifi_Zaptec)
			{

				strcpy(WifiSSID, "ZaptecHQ");
				strcpy(WifiPSK, "LuckyJack#003");
				//strcpy(WifiSSID, "CMW-AP");	Applica Wifi TX test AP without internet connection
				storage_SaveWifiParameters(WifiSSID, WifiPSK);

			}

			else if(switchState == eConfig_Wifi_Home_Wr32)//eConfig_Wifi_Home_Wr32
			{
				if(network_CheckWifiParameters() == false)
				{
					strcpy(WifiSSID, "ZaptecHQ-guest");
					strcpy(WifiPSK, "Ilovezaptec");
					//strcpy(WifiSSID, "BVb");
					//strcpy(WifiPSK, "tk51mo79");
					storage_SaveWifiParameters(WifiSSID, WifiPSK);
				}
			}
			else if(switchState == 4) //Applica - EMC config
			{
				strcpy(WifiSSID, "APPLICA-GJEST");
				strcpy(WifiPSK, "2Sykkelturer!Varmen");//Used during EMC test. Expires in 2021.
				storage_SaveWifiParameters(WifiSSID, WifiPSK);
			}

			if(storage_ReadConfiguration() != ESP_OK)
			{
				storage_Init_Configuration();
				storage_Set_CommunicationMode(eCONNECTION_WIFI);
				storage_SaveConfiguration();
			}

		}
    }
#endif

    // Read connection mode from flash and start interface
    connectivity_init(switchState);


//    if((switchState == eConfig_4G) || (switchState == eConfig_4G_Post))
//    {
//    	ppp_task_start();
//    }

    if(switchState == eConfig_4G_bridge)
	{
		hard_reset_cellular();
	}

	//vTaskDelay(pdMS_TO_TICKS(3000));



//#define WriteThisDeviceInfo
//#define Erase

#ifdef Erase
	EEPROM_Erase();
#endif

#ifdef WriteThisDeviceInfo
	volatile struct DeviceInfo writeDevInfo;
	writeDevInfo.EEPROMFormatVersion = 1;
//	strcpy(writeDevInfo.serialNumber, "ZAP000002");
//	strcpy(writeDevInfo.PSK, "mikfgBtUnIbuoSyCwXjUwgF29KONrGIy5H/RbpGTtdo=");
//	strcpy(writeDevInfo.Pin, "0625");

//	strcpy(writeDevInfo.serialNumber, "ZAP000005");
//	strcpy(writeDevInfo.PSK, "vHZdbNkcPhqJRS9pqEaokFv1CrKN1i2opy4qzikyTOM=");
//	strcpy(writeDevInfo.Pin, "4284");

	strcpy(writeDevInfo.serialNumber, "ZAP000008");
	strcpy(writeDevInfo.PSK, "U66fdr9lD0rkc0fOLL9/253H9Nc/34qEaDUJiEItSks=");
	strcpy(writeDevInfo.Pin, "7833");

//	strcpy(writeDevInfo.serialNumber, "ZAP000010");
//	strcpy(writeDevInfo.PSK, "rvop1J1GQMsR91puAZLuUs3nTMzf02UvNA83WDWMuz0=");
//	strcpy(writeDevInfo.Pin, "6695");

	i2cWriteDeviceInfoToEEPROM(writeDevInfo);
#endif

	struct DeviceInfo devInfo;
	if(switchState != eConfig_Wifi_Home_Wr32)
	{
		devInfo = i2cReadDeviceInfoFromEEPROM();
		if(devInfo.EEPROMFormatVersion == 0xFF)
		{
			//Invalid EEPROM content
			prodtest_getNewId();

			devInfo = i2cReadDeviceInfoFromEEPROM();
		}
		else if(devInfo.EEPROMFormatVersion == 0x0)
		{
			ESP_LOGE(TAG, "Invalid EEPROM format: %d", devInfo.EEPROMFormatVersion);

			vTaskDelay(3000 / portTICK_PERIOD_MS);
		}

		I2CDevicesStartTask();
	}
	else
	{
		//Wroom32 ID - BLE - (no EEPROM)
		strcpy(devInfo.serialNumber, "ZAP000011");
		strcpy(devInfo.PSK, "eBApJr3SKRbXgLpoJEpnLA+nRK508R3i/yBKroFD1XM=");
		strcpy(devInfo.Pin, "7053");

		//Wroom32 ID - BLE - (no EEPROM)
//		strcpy(devInfo.serialNumber, "ZAP000012");
//		strcpy(devInfo.PSK, "+cype9l6QpYa4Yf375ZuftuzM7PDtso5KvGv08/7f0A=");
//		strcpy(devInfo.Pin, "5662");

		devInfo.EEPROMFormatVersion = 1;
		i2cSetDebugDeviceInfoToMemory(devInfo);
	}


	vTaskDelay(500 / portTICK_PERIOD_MS);

//	if((switchState == eConfig_Wifi_NVS) ||
//	   (switchState == eConfig_Wifi_Zaptec) ||
//	   (switchState == eConfig_Wifi_EMC) ||
//	   (switchState == eConfig_4G))
//	{
//		start_cloud_listener_task(devInfo);
//	}

	if((switchState == eConfig_Wifi_Post) || (switchState == eConfig_4G_Post))
	{
		SetDataInterval(10);
	}

	ble_interface_init();

	uint32_t ledState = 0;

    gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    //if((switchState != eConfig_4G) && (switchState != eConfig_4G_bridge))
    	//diagnostics_port_init();

    if( (switchState != eConfig_4G_bridge))
    	sessionHandler_init();

    size_t free_heap_size_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    uint32_t counter = 0;

    int secleft = 0;
    int min = 0;
    int hours = 0;
    int days = 0;

    while (true)
    {
    	counter++;

    	if(ledState == 0)
    		ledState = 1;
    	else
    		ledState = 0;

    	gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    	if(counter % 10 == 0)
    	{
    		days = counter / 86400;
    		secleft = counter % 86400;

    		hours = secleft / 3600;
    		secleft = secleft % 3600;

    		min = secleft / 60;
    		secleft = secleft % 60;

    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    		ESP_LOGE(TAG, "%d: %dd %02dh%02dm%02ds %s , rst: %d, Heaps: %i %i, Sw: %i", counter, days, hours, min, secleft, softwareVersion, esp_reset_reason(), free_heap_size_start, (free_heap_size_start-free_heap_size), switchState);
    	}

    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

