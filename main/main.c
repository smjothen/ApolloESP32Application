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
#include "esp_ota_ops.h"

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
#include "certificate.h"
#include "fat.h"
#include "cJSON.h"
#include "zaptec_cloud_listener.h"
#include "sas_token.h"
#include "offlineSession.h"
#include "zaptec_cloud_observations.h"
#ifdef useAdvancedConsole
	//#include "apollo_console.h"
#endif

static const char *TAG_MAIN = "MAIN           ";

//OUTPUT PIN
#define GPIO_OUTPUT_DEBUG_LED    0
#define GPIO_OUTPUT_DEBUG_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED)

uint32_t onTimeCounter = 0;
char softwareVersion[] = "2.0.0.166";

uint8_t GetEEPROMFormatVersion()
{
	return 1;
}

char * GetSoftwareVersion()
{
	return softwareVersion;
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


	// Initialize debug led pin to 0. Should not be high at boot
	gpio_set_level(GPIO_OUTPUT_DEBUG_LED, 1);

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

	int length = uart_read_bytes(UART_NUM_0, uart_data, 1, 1);
	if(length > 0)
	{
		memcpy(commandBuffer+strlen(commandBuffer), uart_data, length);
		ESP_LOGW(TAG_MAIN, "Read: %s", commandBuffer);
	}
	if(strchr(commandBuffer, '\r') != NULL)
	{
		ESP_LOGW(TAG_MAIN, "Command:> %s", commandBuffer);

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

		else if(strncmp("rst", commandBuffer, 3) == 0)
			esp_restart();
		else if(strncmp("dtr0", commandBuffer, 4) == 0)
			gpio_set_level(GPIO_OUTPUT_DTR, 0);
		else if(strncmp("dtr1", commandBuffer, 4) == 0)
			gpio_set_level(GPIO_OUTPUT_DTR, 1);
		else if(strncmp("sdtr", commandBuffer, 4) == 0)
			at_command_with_ok_ack("AT&D1", 1000);


		else if(strncmp("latest", commandBuffer, 6) == 0)
			offlineSession_FindNewFileNumber();

		else if(strncmp("oldest", commandBuffer, 6) == 0)
			offlineSession_FindOldestFile();

		else if(strncmp("nrof", commandBuffer, 4) == 0)
			offlineSession_FindNrOfFiles();

		else if(strncmp("cont", commandBuffer, 4) == 0)
		{
			int x = 0;
			if(sscanf(&commandBuffer[5], "%d", &x))
			{
				ESP_LOGW(TAG_MAIN, "Reading file no content: %d", x);
				//char * fileBuffer = calloc(20000,1);
				offlineSession_Diagnostics_ReadFileContent(x);//, fileBuffer);
				//ESP_LOGW(TAG_MAIN, "fileBuffer: \r\n %s", fileBuffer);
				//free(fileBuffer);
			}
		}


		else if(strncmp("ab", commandBuffer, 2) == 0)
		{
			time_t now = 0;
			time(&now);
			static float s_energy = 0.1;
			offlineSession_append_energy('B', now, s_energy++);
		}
		else if(strncmp("ae", commandBuffer, 2) == 0)
		{
			time_t now = 0;
			time(&now);
			static float s_energy = 0.1;
			offlineSession_append_energy('E', now, s_energy++);
		}
		else if(strncmp("end", commandBuffer, 3) == 0)
		{
			ESP_LOGW(TAG_MAIN, "Ending session");
			//offlineSession_end();
		}

		else if(strncmp("del", commandBuffer, 3) == 0)
		{
			int x = 0;
			if(sscanf(&commandBuffer[5], "%d", &x))
			{
				ESP_LOGW(TAG_MAIN, "Deleting file no : %d", x);
				offlineSession_delete_session(x);
			}
		}

		else if(strncmp("delall", commandBuffer, 6) == 0)
		{
			ESP_LOGW(TAG_MAIN, "Deleting all files");
			int fileNo;
			for (fileNo = 0; fileNo < 100; fileNo++)
			{
				offlineSession_delete_session(fileNo);
			}
		}

		memset(commandBuffer, 0, 10);
	}


}
//#define useSimpleConsole


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


//Function used by cJSON to allocate on SPI-memory
void * ext_calloc(size_t size)
{
	return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void cJSON_Init_Memory()
{
	cJSON_Hooks memoryHook;

	memoryHook.malloc_fn = ext_calloc;
	memoryHook.free_fn = free;
	cJSON_InitHooks(&memoryHook);
}


static bool onlineWatchdog = false;
static uint32_t onlineWatchdogCounter = 0;

void SetOnlineWatchdog()
{
	onlineWatchdog = true;
}



void app_main(void)
{
	ESP_LOGE(TAG_MAIN, "Zaptec Go: %s, %s, (tag/commit %s)", softwareVersion, OTAReadRunningPartition(), esp_ota_get_app_description()->version);

#ifdef DEVELOPEMENT_URL
	ESP_LOGE(TAG_MAIN, "DEVELOPEMENT URLS USED");
#else
	//PROD url used
#endif

#ifdef RUN_FACTORY_TESTS
	ESP_LOGE(TAG_MAIN, "####### FACTORY TEST MODE ACTIVE!!! ##########");
#endif

#ifndef ENABLE_LOGGING
	//Logging disabled
	esp_log_level_set("*", ESP_LOG_NONE);
#else
	//Logging enabled
#endif

	//First check hardware revision in order to configure io accordingly
	adc_init();

	eeprom_wp_pint_init();
	cellularPinsInit();

#ifdef useAdvancedConsole
	gpio_pullup_en(GPIO_NUM_3);
	apollo_console_init();
#endif

	eeprom_wp_enable_nfc_enable();
	InitGPIOs();

	//Call before using cJSON-lib to make it us SPI memory
	cJSON_Init_Memory();

	storage_Init();

	if(storage_ReadConfiguration() != ESP_OK)
	{
		ESP_LOGE(TAG_MAIN, "########## Invalid or no parameters in storage! ########");

		storage_Init_Configuration();
		storage_SaveConfiguration();
	}


	if(storage_Get_DiagnosticsMode() == eACTIVATE_LOGGING)
	{
		esp_log_level_set("*", ESP_LOG_INFO);
	}

	else if((storage_Get_DiagnosticsMode() == eDISABLE_CERTIFICATE_ONCE) || (storage_Get_DiagnosticsMode() == eDISABLE_CERTIFICATE_ALWAYS))
	{
		certificate_SetUsage(false);
		ESP_LOGE(TAG_MAIN, "Certificates disabled");
	}

	//Ensure previous versions not supporting RFID requires authentication if set incorrectly
	storage_Verify_AuthenticationSetting();

	storage_PrintConfiguration();

	//Init to read device ID from EEPROM
	I2CDevicesInit();

#ifdef useSimpleConsole
	configure_console();
#endif

	ppp_configure_uart(); //Remove since in connectivity?
	start_ota_task();
    zaptecProtocolStart();

    validate_booted_image();

	// The validate_booted_image() must sync the dsPIC FW before we canstart the polling
	dspic_periodic_poll_start();

    vTaskDelay(pdMS_TO_TICKS(3000));


//#define BG_BRIDGE
#ifdef BG_BRIDGE
	cellularPinsOn();
#endif


//#define WriteThisDeviceInfo
//#define Erase

#ifdef Erase
	EEPROM_Erase();
#endif

#ifdef WriteThisDeviceInfo
	volatile struct DeviceInfo writeDevInfo;
	writeDevInfo.EEPROMFormatVersion = 1;
	strcpy(writeDevInfo.serialNumber, "");
	strcpy(writeDevInfo.PSK, "");
	strcpy(writeDevInfo.Pin, "");
	eeprom_wp_disable_nfc_disable();
	i2cWriteDeviceInfoToEEPROM(writeDevInfo);
	eeprom_wp_enable_nfc_enable();
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
	EEPROM_WriteFactoryStage(FactoryStageFinnished);
	eeprom_wp_enable_nfc_enable();
	#endif

	fat_static_mount();

	i2cReadDeviceInfoFromEEPROM();
	I2CDevicesStartTask();

	struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();

	if((storage_Get_CommunicationMode() == eCONNECTION_LTE) && (devInfo.factory_stage == FactoryStageFinnished))
	{
		//Toggling 4G to ensure a clean 4G initialization
		//If it was ON at restart it will be power OFF now and ON again later.
		//If it was OFF this will effectively power it ON so it is ready for later.
		cellularPinsOff();
	}
	
	connectivity_init();

	if(devInfo.EEPROMFormatVersion == 0xFF)
	{
		//Invalid EEPROM content
		int id_result = prodtest_getNewId(false);
		if(id_result<0){
			ESP_LOGE(TAG_MAIN, "ID assign failed");
			vTaskDelay(pdMS_TO_TICKS(500));
			esp_restart();
		}
		devInfo = i2cReadDeviceInfoFromEEPROM();
		//new_id = true;
		
		if(devInfo.EEPROMFormatVersion == 0x0)
		{
			ESP_LOGE(TAG_MAIN, "Invalid EEPROM format: %d", devInfo.EEPROMFormatVersion);
			vTaskDelay(3000 / portTICK_PERIOD_MS);
			esp_restart();
		}
	}


	if(devInfo.factory_stage != FactoryStageFinnished){

		// do not verify zapno until we can resolve ZapProgram issue
		//int prodtest_result = prodtest_perform(devInfo, new_id);
		int prodtest_result = prodtest_perform(devInfo, true);

		if(prodtest_result<0){
			ESP_LOGE(TAG_MAIN, "Prodtest failed");
			esp_restart();
		}
	}

	ble_interface_init();


    //#define DIAGNOSTICS //Enable TCP port for EMC diagnostics
    //#ifdef DIAGNOSTICS
	//Allow remote activation for use with lab-testsetup
	if((storage_Get_DiagnosticsMode() == eACTIVATE_TCP_PORT) && (storage_Get_CommunicationMode() == eCONNECTION_WIFI))
	{
		esp_log_level_set("*", ESP_LOG_INFO);
		diagnostics_port_init();
		ESP_LOGE(TAG_MAIN, "TCP PORT ACTIVATED");
	}
	//#endif

    #ifndef BG_BRIDGE
    sessionHandler_init();
	#endif

    size_t free_heap_size_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    char onTimeString[20]= {0};

    bool hasBeenOnline = false;
    int otaDelayCounter = 0;
    int lowMemCounter = 0;

	while (true)
    {
		onTimeCounter++;

		///For diagnostics
		//ota_time_left();

    	if(onTimeCounter % 10 == 0)
    	{
			size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
			size_t min_dma = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
			size_t blk_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
			
			//If available memory is critically low, to a controlled restart to avoid undefined insufficient memory states
			if((min_dma < 2000) || (free_dma < 2000))
			{
				lowMemCounter++;
				if(lowMemCounter >= 30)
				{
					ESP_LOGE(TAG_MAIN, "LOW MEM - RESTARTING");
					storage_Set_And_Save_DiagnosticsLog("#12 Low dma mem. Memory leak?");
					esp_restart();
				}
			}

			ESP_LOGI(TAG_MAIN, "DMA memory free: %d, min: %d, largest block: %d", free_dma, min_dma, blk_dma);
    	}

    	if(onTimeCounter % 10 == 0)
    	{
    		ESP_LOGI(TAG_MAIN, "Stacks: i2c:%d mcu:%d %d adc: %d, lte: %d conn: %d, sess: %d, ocmf: %d", I2CGetStackWatermark(), MCURxGetStackWatermark(), MCUTxGetStackWatermark(), adcGetStackWatermark(), pppGetStackWatermark(), connectivity_GetStackWatermark(), sessionHandler_GetStackWatermark(), sessionHandler_GetStackWatermarkOCMF());

    		GetTimeOnString(onTimeString);
    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
			size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
			size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
			size_t blk_dram = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    		ESP_LOGI(TAG_MAIN, "%d: %s %s , rst: %d, Heaps: %i %i DRAM: %i Lo: %i, Blk: %i, Sw: %i", onTimeCounter, onTimeString, softwareVersion, esp_reset_reason(), free_heap_size_start, free_heap_size, free_dram, low_dram, blk_dram, MCU_GetSwitchState());
    	}


    	if (isMqttConnected() == true)
    	{
			if(onTimeCounter % (554400) == 0) //Refreshing after 3300 * 24 * 7 seconds. Token valid for 3600 * 24 * 7 seconds
			{
				/// If this is not called, the token will expire, the charger will be disconnected and do an reconnect after 10 seconds
				/// Doing token refresh and reconnect in advance gives a more stable connection.
				periodic_refresh_token(1);
			}
    	}

    	/// Experimental
    	/// If mqtt is running and EVENT_ERROR increment, try to call the refresh token which also results in a mqtt start/stop sequence
    	/// Verify if this successfully generates a reconnect.
    	if(connectivity_GetMQTTInitialized() && (cloud_listener_GetResetCounter() > 0))
    	{
			if(cloud_listener_GetResetCounter() % 7 == 0)
			{
				///Increment to avoid retrigging this case
				cloud_listener_IncrementResetCounter();

				periodic_refresh_token(2);	//Argument is for diagnostics
			}
    	}


    	//For 4G testing - activated with command
    	if(onlineWatchdog == true)
    	{
    		if(isMqttConnected() == false)
    		{
    			onlineWatchdogCounter++;
    			ESP_LOGI(TAG_MAIN, "OnlineWatchdogCounter : %d", onlineWatchdogCounter);
    		}
    		if(onlineWatchdogCounter == 300)
    		{
    			storage_Set_And_Save_DiagnosticsLog("#7 main.c onlineWatchdogCounter == 300");
    			esp_restart();
    		}
    	}


    	//On rare occasions we have not been able to get online after firmware update on 4G. This sequence checks if we are not online after a 4G firware update and does a full
		//4G and ESP restart to try and get back online. The 4G module will be powered on automatically if 4G is active communication mode.
		//The effekt can be tested with the Debug command "PowerOff4GAndReset"
		if((storage_Get_CommunicationMode() == eCONNECTION_LTE))
		{
			if(onTimeCounter < 600)
			{
				if(isMqttConnected() == true)
				{
					hasBeenOnline = true;
				}
			}
			if(onTimeCounter == 600)
			{
				if((ota_CheckIfHasBeenUpdated() == true) && (hasBeenOnline == false))
				{
					ESP_LOGW(TAG_MAIN, "Not able to get back online after firmware update, powering off 4G and restarting");
					cellularPinsOff();

					storage_Set_And_Save_DiagnosticsLog("#8 main.c LTE: Not online after firmware update");

					esp_restart();
				}
			}
		}


		/// Wait until car disconnects, delay 5 more minutes, then start OTA.
		if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_DISCONNECTED)
		{
			if(IsOTADelayActive())
			{
				otaDelayCounter++;

				if(otaDelayCounter % 10 == 0)
					ESP_LOGW(TAG_MAIN, "OTA Counter: %d", otaDelayCounter);

				/// When delay after disconnect has passed -> perform OTA
				if(otaDelayCounter == 300)
				{
					otaDelayCounter = 0;
					ClearOTADelay();

					InitiateOTASequence();
				}
			}
		}
		else
		{
			/// Reset counter while car is connected
			otaDelayCounter = 0;
		}


	#ifdef useSimpleConsole
		int i;
		for (i = 0; i < 10; i++)
		{
			HandleCommands();
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
	#else
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	#endif

    }
}
