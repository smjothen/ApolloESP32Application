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

#include "main.h"
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
#include "../components/cellular_modem/include/ppp_task.h"
#include "driver/uart.h"
#include "eeprom_wp.h"
#include "apollo_console.h"
#include "certificate.h"
#include "fat.h"

const char *TAG_MAIN = "MAIN     ";

//OUTPUT PIN
#define GPIO_OUTPUT_DEBUG_LED    0

uint32_t onTimeCounter = 0;
char softwareVersion[] = "0.0.0.32";
char softwareVersionBLEtemp[] = "0.0.0.32";	//USED to face ble version

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


// Configure the RX port of UART0(log-port) for commands
void configure_console(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 1024, 0, NULL, 0);
}

char commandBuffer[10] = {0};

#include "at_commands.h"
void HandleCommands()
{
	//Simple commands
	uint8_t uart_data_size = 10;
	uint8_t uart_data[uart_data_size];

	int length = uart_read_bytes(UART_NUM_0, uart_data, 1, 100);
	if(length > 0)
	{
		memcpy(commandBuffer+strlen(commandBuffer), uart_data, length);
		//ESP_LOGW(TAG_MAIN, "Read: %s", commandBuffer);
	}
	if(strchr(commandBuffer, '\r') != NULL)
	{
		if(strncmp("mcu", commandBuffer, 3) == 0)
		{
			if(strchr(commandBuffer, '0') != NULL)
				protocol_task_ctrl_debug(0);
			else
				protocol_task_ctrl_debug(1);
		}

		else if(strncmp("main", commandBuffer, 4) == 0)
		{
			if(strchr(commandBuffer, '0') != NULL)
				esp_log_level_set(TAG_MAIN, ESP_LOG_NONE);
			else
				esp_log_level_set(TAG_MAIN, ESP_LOG_ERROR);
		}

		else if(strncmp("i2c", commandBuffer, 3) == 0)
		{
			if(strchr(commandBuffer, '0') != NULL)
				i2c_ctrl_debug(0);
			else
				i2c_ctrl_debug(1);
		}

		else if(strncmp("m1", commandBuffer, 2) == 0)
			cellularPinsOn();

		else if(strncmp("m0", commandBuffer, 2) == 0)
			cellularPinsOff();

		else if(strncmp("r", commandBuffer, 1) == 0)
			esp_restart();
		else if(strncmp("dtr0", commandBuffer, 4) == 0)
			gpio_set_level(GPIO_OUTPUT_DTR, 0);
		else if(strncmp("dtr1", commandBuffer, 4) == 0)
			gpio_set_level(GPIO_OUTPUT_DTR, 1);
		else if(strncmp("sdtr", commandBuffer, 4) == 0)
			at_command_with_ok_ack("AT&D1", 1000);

		memset(commandBuffer, 0, 10);
	}


}
//#define useConsole



void GetTimeOnString(char * onTimeString)
{
	int secleft = 0;
	int min = 0;
	int hours = 0;
	int days = 0;

	days = onTimeCounter / 86400;
	secleft = onTimeCounter % 86400;

	hours = secleft / 3600;
	secleft = secleft % 3600;

	min = secleft / 60;
	secleft = secleft % 60;

	sprintf(onTimeString, "%dd %02dh%02dm%02ds",days ,hours, min, secleft);
}



void app_main(void)
{
	//First check hardware revision in order to configure io accordingly
	adc_init();

	eeprom_wp_pint_init();
	cellularPinsInit();

	apollo_console_init();

	eeprom_wp_enable_nfc_enable();
	//certificate_init();
	//fat_make();
	//certificateGetNew();
	//certificateValidate();


	ESP_LOGE(TAG_MAIN, "Apollo: %s", OTAReadRunningPartition());

	storage_Init();

	if(storage_ReadConfiguration() != ESP_OK)
	{
		ESP_LOGE(TAG_MAIN, "########## Invalid or no parameters in storage! ########");

		storage_Init_Configuration();
		storage_SaveConfiguration();
	}

	storage_PrintConfiguration();

	//Init to read device ID from EEPROM
	I2CDevicesInit();

#ifdef useConsole
	configure_console();
#endif

	configure_uart();
    zaptecProtocolStart();

	validate_booted_image();

	// The validate_booted_image() must sync the dsPIC FW before we canstart the polling
	dspic_periodic_poll_start(); 

    vTaskDelay(pdMS_TO_TICKS(3000));

//#define DEV
#ifdef DEV
    int switchState = 0;
    switchState = MCU_GetSwitchState();

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
				storage_Set_CommunicationMode(eCONNECTION_WIFI);
				storage_SaveConfiguration();

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
					storage_Set_CommunicationMode(eCONNECTION_WIFI);
					storage_SaveConfiguration();
				}
			}
			else if(switchState == 4) //Applica - EMC config
			{
				strcpy(WifiSSID, "APPLICA-GJEST");
				strcpy(WifiPSK, "2Sykkelturer!Varmen");//Used during EMC test. Expires in 2021.
				storage_SaveWifiParameters(WifiSSID, WifiPSK);
				storage_Set_CommunicationMode(eCONNECTION_WIFI);
				storage_SaveConfiguration();

			}

//			if(storage_ReadConfiguration() != ESP_OK)
//			{
//				ESP_LOGE(TAG_MAIN, "########## Invalid or no parameters in storage! ########");
//
//				storage_Init_Configuration();
//				storage_Set_CommunicationMode(eCONNECTION_WIFI);
//				storage_SaveConfiguration();
//			}

		}
    }


    if((switchState == eConfig_4G) || (switchState == eConfig_4G_Post))
    {
    	storage_Set_CommunicationMode(eCONNECTION_LTE);
		storage_SaveConfiguration();
    }
#endif

//#define BG_BRIDGE
#ifdef BG_BRIDGE
	cellularPinsOn();
#endif

    // Read connection mode from flash and start interface
    //connectivity_init();


//#define WriteThisDeviceInfo
//#define Erase

#ifdef Erase
	EEPROM_Erase();
#endif

#ifdef WriteThisDeviceInfo
	volatile struct DeviceInfo writeDevInfo;
	writeDevInfo.EEPROMFormatVersion = 1;

//	strcpy(writeDevInfo.serialNumber, "ZAP000005");
//	strcpy(writeDevInfo.PSK, "vHZdbNkcPhqJRS9pqEaokFv1CrKN1i2opy4qzikyTOM=");
//	strcpy(writeDevInfo.Pin, "4284");

	i2cWriteDeviceInfoToEEPROM(writeDevInfo);
#endif

	// #define FORCE_NEW_ID
	#ifdef FORCE_NEW_ID
	eeprom_wp_disable_nfc_disable();
	EEPROM_WriteFormatVersion(0xFF);
	eeprom_wp_enable_nfc_enable();
	#endif

	// #define FORCE_FACTORY_TEST
	#ifdef FORCE_FACTORY_TEST
	eeprom_wp_disable_nfc_disable();
	EEPROM_WriteFactoryStage(FactoryStageUnknown2);
	eeprom_wp_enable_nfc_enable();
	#endif

	struct DeviceInfo devInfo;
	devInfo = i2cReadDeviceInfoFromEEPROM();
	if(devInfo.EEPROMFormatVersion == 0xFF)
	{
		devInfo = i2cReadDeviceInfoFromEEPROM();
		I2CDevicesStartTask();
		if(devInfo.EEPROMFormatVersion == 0xFF)
		{
			//Invalid EEPROM content
			int id_result = prodtest_getNewId();
			if(id_result<0){
				ESP_LOGE(TAG_MAIN, "ID assign failed");
				vTaskDelay(pdMS_TO_TICKS(500));
				esp_restart();
			}
			devInfo = i2cReadDeviceInfoFromEEPROM();
		}
		else if(devInfo.EEPROMFormatVersion == 0x0)
		{
			ESP_LOGE(TAG_MAIN, "Invalid EEPROM format: %d", devInfo.EEPROMFormatVersion);

		vTaskDelay(3000 / portTICK_PERIOD_MS);
	}


		if(devInfo.factory_stage != FactoryStageFinnished){
			int prodtest_result = prodtest_perform(devInfo);
			if(prodtest_result<0){
				ESP_LOGE(TAG_MAIN, "Prodtest failed");
				esp_restart();
			}
		}
	I2CDevicesStartTask();

    // Read connection mode from flash and start interface
    connectivity_init();

	/*if(devInfo.factory_stage != FactoryStageFinnished){
		gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 0);
		prodtest_perform(devInfo);
		gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 1);
	}*/

	/*}
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
	}*/

	vTaskDelay(500 / portTICK_PERIOD_MS);

	ble_interface_init();

	uint32_t ledState = 0;

    gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    //#define DIAGNOSTICS //Enable TCP port for EMC diagnostics
    #ifdef DIAGNOSTICS
    	diagnostics_port_init();
	#endif

    #ifndef BG_BRIDGE
    sessionHandler_init();
	#endif

    size_t free_heap_size_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);


    /*int secleft = 0;
    int min = 0;
    int hours = 0;
    int days = 0;*/
	
	//certificateGetNew();
    //certificate_init();

    char onTimeString[20]= {0};

	while (true)
    {
		onTimeCounter++;

    	if(ledState == 0)
    		ledState = 1;
    	else
    		ledState = 0;

    	//gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    	if(onTimeCounter % 5 == 0)
    	{
    		/*days = onTimeCounter / 86400;
    		secleft = onTimeCounter % 86400;

    		hours = secleft / 3600;
    		secleft = secleft % 3600;

    		min = secleft / 60;
    		secleft = secleft % 60;*/

    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    		size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    		size_t blk_dram = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
			
			size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
			size_t min_dma = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
			size_t blk_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
			
			ESP_LOGI(TAG_MAIN, "[DMA memory] free: %d, min: %d, largest block: %d", free_dma, min_dma, blk_dma);
			ESP_LOGI(TAG_MAIN, "Stacks: i2c:%d mcu:%d %d adc: %d, lte: %d conn: %d, sess: %d", I2CGetStackWatermark(), MCURxGetStackWatermark(), MCUTxGetStackWatermark(), adcGetStackWatermark(), pppGetStackWatermark(), connectivity_GetStackWatermark(), sessionHandler_GetStackWatermark());

    		GetTimeOnString(onTimeString);
    		ESP_LOGI(TAG_MAIN, "%d: %s %s , rst: %d, Heaps: %i %i DRAM: %i Lo: %i, Blk: %i, Sw: %i", onTimeCounter, onTimeString, softwareVersion, esp_reset_reason(), free_heap_size_start, free_heap_size, free_dram, low_dram, blk_dram, MCU_GetSwitchState());
    	}

	#ifdef useConsole
    	HandleCommands();
	#endif



    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}



