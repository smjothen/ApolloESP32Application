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
//#include "apollo_console.h"
#include "certificate.h"
#include "fat.h"
#include "cJSON.h"
#include "zaptec_cloud_listener.h"
#include "sas_token.h"

const char *TAG_MAIN = "MAIN     ";

//OUTPUT PIN
#define GPIO_OUTPUT_DEBUG_LED    0
#define GPIO_OUTPUT_DEBUG_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED)

uint32_t onTimeCounter = 0;
char softwareVersion[] = "0.0.2.4";

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
	ESP_LOGE(TAG_MAIN, "DEVELOPEMENT URL USED");
#endif

#ifdef DISABLE_LOGGING
	esp_log_level_set("*", ESP_LOG_NONE);
#endif

	//First check hardware revision in order to configure io accordingly
	adc_init();

	eeprom_wp_pint_init();
	cellularPinsInit();

	//gpio_pullup_en(GPIO_NUM_3);
	//apollo_console_init();

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

#ifdef useConsole
	configure_console();
#endif

	configure_uart();
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

	fat_static_mount();

	i2cReadDeviceInfoFromEEPROM();
	I2CDevicesStartTask();

	struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();

	if((storage_Get_CommunicationMode() == eCONNECTION_LTE) && (devInfo.factory_stage == FactoryStageFinnished))
	{
		//Toggling 4G to ensure a clean 4G initialization
		//If it was ON at restart it will be power OFF now and ON again later.
		//If it was OFF this will effectively power it ON so it is ready for later.
		//cellularPinsOff();
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

	while (true)
    {
		onTimeCounter++;

    	if(onTimeCounter % 15 == 0)//15
    	{
			size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
			size_t min_dma = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
			size_t blk_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
			
			ESP_LOGW(TAG_MAIN, "[DMA memory] free: %d, min: %d, largest block: %d", free_dma, min_dma, blk_dma);
    	}

    	if(onTimeCounter % 15 == 0)//15
    	{
    		ESP_LOGI(TAG_MAIN, "Stacks: i2c:%d mcu:%d %d adc: %d, lte: %d conn: %d, sess: %d", I2CGetStackWatermark(), MCURxGetStackWatermark(), MCUTxGetStackWatermark(), adcGetStackWatermark(), pppGetStackWatermark(), connectivity_GetStackWatermark(), sessionHandler_GetStackWatermark());

    		GetTimeOnString(onTimeString);
    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
			size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
			size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
			size_t blk_dram = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    		ESP_LOGI(TAG_MAIN, "%d: %s %s , rst: %d, Heaps: %i %i DRAM: %i Lo: %i, Blk: %i, Sw: %i", onTimeCounter, onTimeString, softwareVersion, esp_reset_reason(), free_heap_size_start, free_heap_size, free_dram, low_dram, blk_dram, MCU_GetSwitchState());
    	}

    	/*if(onTimeCounter % 100 == 0)
    	{
    		periodic_refresh_token();
    	}*/

    	//For 4G testing - activated with command
    	if(onlineWatchdog == true)
    	{
    		if(isMqttConnected() == false)
    		{
    			onlineWatchdogCounter++;
    			ESP_LOGI(TAG_MAIN, "OnlineWatchdogCounter : %d", onlineWatchdogCounter);
    		}
    		if(onlineWatchdogCounter == 300)
    			esp_restart();
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
					esp_restart();
				}
			}
		}


	#ifdef useConsole
    	HandleCommands();
	#endif

    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
