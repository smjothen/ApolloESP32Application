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
#include "../components/cellular_modem/include/ppp_task.h"
#include "driver/uart.h"
#include "eeprom_wp.h"

const char *TAG_MAIN = "MAIN     ";

//OUTPUT PIN
#define GPIO_OUTPUT_DEBUG_LED    0


char softwareVersion[] = " 0.0.0.4 ";
char softwareVersionBLEtemp[] = "2.8.0.2";	//USED to fake ble version

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
     //   .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
       // .rx_flow_ctrl_thresh = 120,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    //uart_set_pin(UART_NUM_1, ECHO_TEST_TXD1, ECHO_TEST_RXD1, ECHO_TEST_RTS1, ECHO_TEST_CTS1);
    uart_driver_install(UART_NUM_0, 1024, 1024, NULL, NULL, 0);
}

char commandBuffer[10] = {0};

#include "at_commands.h"
void HandleCommands()
{
	//Simple commands
	uint8_t uart_data_size = 10;
	uint8_t uart_data[uart_data_size];
	volatile int ret = 0;

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
			ret = at_command_with_ok_ack("AT&D1", 1000);

		memset(commandBuffer, 0, 10);
	}


}
//#define useConsole

void app_main(void)
{
	//First check hardware revision in order to configure io accordingly
	adc_init();

	eeprom_wp_pint_init();
	cellularPinsInit();

//	volatile char inputString[] = "+QCCID: 89470060200213074802";
//	volatile char outputString[21] = {0};
//	volatile int ret = GetNumberAsString(inputString, outputString, 20);

	eeprom_wp_enable_nfc_enable();

	ESP_LOGE(TAG_MAIN, "Apollo multi-mode");

	storage_Init();

	if(storage_ReadConfiguration() != ESP_OK)
	{
		ESP_LOGE(TAG_MAIN, "########## Invalid or no parameters in storage! ########");

		storage_Init_Configuration();
		storage_Set_CommunicationMode(eCONNECTION_WIFI);
		int errors = storage_SaveConfiguration();
		ESP_LOGI(TAG_MAIN, "storage initial save, errors: %d", errors);
	}

	//Init to read device ID from EEPROM
	I2CDevicesInit();
#ifdef useConsole
	configure_console();
#endif
	configure_uart();
    zaptecProtocolStart();

    start_ota_task();
	validate_booted_image();
	// validate_booted_image() must sync the dsPIC FW before we canstart the polling
	dspic_periodic_poll_start(); 

    vTaskDelay(pdMS_TO_TICKS(3000));


#define DEV
#ifdef DEV
    int switchState = MCU_GetSwitchState();
	//switchState = eConfig_Wifi_Zaptec;

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
#endif


    if((switchState == eConfig_4G) || (switchState == eConfig_4G_Post))
    {
    	storage_Set_CommunicationMode(eCONNECTION_LTE);
		storage_SaveConfiguration();
    }

    if(switchState == eConfig_4G_bridge)
	{
		hard_reset_cellular();
	}

    // Read connection mode from flash and start interface
    connectivity_init(switchState);


// #define WriteThisDeviceInfo
// #define Erase

#ifdef Erase
	EEPROM_Erase();
#endif

#ifdef WriteThisDeviceInfo
	volatile struct DeviceInfo writeDevInfo;
	writeDevInfo.EEPROMFormatVersion = 1;

	// strcpy(writeDevInfo.serialNumber, "ZAP000001");
	// strcpy(writeDevInfo.PSK, "ubTCXZJoEs8LjFw3lVFzSLXQ0CCJDEiNt7AyqbvxwFA=");
	// strcpy(writeDevInfo.Pin, "2121");

//	strcpy(writeDevInfo.serialNumber, "ZAP000002");
//	strcpy(writeDevInfo.PSK, "mikfgBtUnIbuoSyCwXjUwgF29KONrGIy5H/RbpGTtdo=");
//	strcpy(writeDevInfo.Pin, "0625");

//	strcpy(writeDevInfo.serialNumber, "ZAP000005");
//	strcpy(writeDevInfo.PSK, "vHZdbNkcPhqJRS9pqEaokFv1CrKN1i2opy4qzikyTOM=");
//	strcpy(writeDevInfo.Pin, "4284");

	// strcpy(writeDevInfo.serialNumber, "ZAP000008");
	// strcpy(writeDevInfo.PSK, "U66fdr9lD0rkc0fOLL9/253H9Nc/34qEaDUJiEItSks=");
	// strcpy(writeDevInfo.Pin, "7833");

	strcpy(writeDevInfo.serialNumber, "ZAP000020");
	strcpy(writeDevInfo.PSK, "z4J8JqxPu51JlP8ewyD2KyMbxLUrXYg8PneWBtgEct8=");
	strcpy(writeDevInfo.Pin, "6557");

	// strcpy(writeDevInfo.serialNumber, "ZAP000022");
	// strcpy(writeDevInfo.PSK, "SSuUTcIkWJFJ0wqbXUcQ/6KG5Nom6yQd4L7NFqbN+lc=");
	// strcpy(writeDevInfo.Pin, "9451");

	i2cWriteDeviceInfoToEEPROM(writeDevInfo);
#endif

	#define FORCE_FACTORY_TEST
	#ifdef FORCE_FACTORY_TEST
	eeprom_wp_disable_nfc_disable();
	EEPROM_WriteFactoryStage(FactoryStageUnknown2);
	eeprom_wp_enable_nfc_enable();
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
			ESP_LOGE(TAG_MAIN, "Invalid EEPROM format: %d", devInfo.EEPROMFormatVersion);

			vTaskDelay(3000 / portTICK_PERIOD_MS);
		}

		I2CDevicesStartTask();

		if(devInfo.factory_stage != FactoryStageFinnished){
			prodtest_perform(devInfo);
		}

	}
	else
	{
		//Wroom32 ID - BLE - (no EEPROM)
		//strcpy(devInfo.serialNumber, "ZAP000011");
		//strcpy(devInfo.PSK, "eBApJr3SKRbXgLpoJEpnLA+nRK508R3i/yBKroFD1XM=");
		//strcpy(devInfo.Pin, "7053");

		//Wroom32 ID - BLE - (no EEPROM)
//		strcpy(devInfo.serialNumber, "ZAP000012");
//		strcpy(devInfo.PSK, "+cype9l6QpYa4Yf375ZuftuzM7PDtso5KvGv08/7f0A=");
//		strcpy(devInfo.Pin, "5662");

		devInfo.EEPROMFormatVersion = 1;
		i2cSetDebugDeviceInfoToMemory(devInfo);
	}

	vTaskDelay(500 / portTICK_PERIOD_MS);

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

    	//gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    	if(counter % 5 == 0)
    	{
    		days = counter / 86400;
    		secleft = counter % 86400;

    		hours = secleft / 3600;
    		secleft = secleft % 3600;

    		min = secleft / 60;
    		secleft = secleft % 60;

    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    		size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    		size_t blk_dram = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    		ESP_LOGE(TAG_MAIN, "%d: %dd %02dh%02dm%02ds %s , rst: %d, Heaps: %i %i DRAM: %i Lo: %i, Blk: %i, Sw: %i", counter, days, hours, min, secleft, softwareVersion, esp_reset_reason(), free_heap_size_start, free_heap_size, free_dram, low_dram, blk_dram, switchState);

    		ESP_LOGW(TAG_MAIN, "Stacks: i2c:%d mcu:%d %d adc: %d, lte: %d conn: %d, sess: %d", I2CGetStackWatermark(), MCURxGetStackWatermark(), MCUTxGetStackWatermark(), adcGetStackWatermark(), pppGetStackWatermark(), connectivity_GetStackWatermark(), sessionHandler_GetStackWatermark());
    		//ESP_LOGE(TAG, "%d: %dd %02dh%02dm%02ds %s , rst: %d, Heaps: %i %i, Sw: %i", counter, days, hours, min, secleft, softwareVersion, esp_reset_reason(), free_heap_size_start, (free_heap_size_start-free_heap_size), switchState);
    	}

    	//Until BLE driver error is resolved, disable ble after 1 hour.
//    	if(counter == 60)//3600)
//    	{
//    		ESP_LOGW(TAG,"Deinitializing BLE");
//    		ble_interface_deinit();
//    	}
	#ifdef useConsole
    	HandleCommands();
	#endif

    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}




