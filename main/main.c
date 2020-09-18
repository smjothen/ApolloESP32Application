/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

//#include "protocol_examples_common.h"
#include "esp_websocket_client.h"
#include "protocol_task.h"
#include "ppp_task.h"

//#include "ocpp_task.h"

#include "adc_control.h"
#include "driver/ledc.h"
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

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_TEST_CH_NUM       (1)


static const char *TAG = "MAIN     ";



//OUTPUT PINTS
//LED
#define GPIO_OUTPUT_DEBUG_LED    0
#define GPIO_OUTPUT_PWRKEY		21
#define GPIO_OUTPUT_RESET		33
//AUDIO
#define LEDC_TEST_CH_NUM_E 0
#define GPIO_OUTPUT_AUDIO   (2)

//#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED | 1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET)
#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED | 1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET | 1ULL<<GPIO_OUTPUT_AUDIO)

//INPUT PINS
#define GPIO_INPUT_nHALL_FX    4
#define GPIO_INPUT_nNFC_IRQ    36
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_nHALL_FX) | (1ULL<<GPIO_INPUT_nNFC_IRQ))
#define ESP_INTR_FLAG_DEFAULT 0



void InitGPIOs()
{

//	gpio_config_t io_conf;
//	//disable interrupt
//	io_conf.intr_type = GPIO_PIN_INTR_ANYEDGE;//GPIO_PIN_INTR_DISABLE;
//	 //bit mask of the pins, use GPIO4/5 here
//	io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
//	//set as input mode
//	io_conf.mode = GPIO_MODE_INPUT;
//	//enable pull-up mode
//	io_conf.pull_up_en = 0;
//	gpio_config(&io_conf);


	//gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_ANYEDGE);

	//create a queue to handle gpio event from isr
	//gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
	//start gpio task
	//xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

	//install gpio isr service
	//gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	//hook isr handler for specific gpio pin
	//gpio_isr_handler_add(GPIO_INPUT_nHALL_FX, gpio_isr_handler, (void*) GPIO_INPUT_nHALL_FX);
	//hook isr handler for specific gpio pin
	//gpio_isr_handler_add(GPIO_INPUT_nNFC_IRQ, gpio_isr_handler, (void*) GPIO_INPUT_nNFC_IRQ);

	//remove isr handler for gpio number.
	//gpio_isr_handler_remove(GPIO_INPUT_IO_0);
	//hook isr handler for specific gpio pin again
	//gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);

    gpio_config_t output_conf;
	output_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	output_conf.mode = GPIO_MODE_OUTPUT;
	output_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	output_conf.pull_down_en = 0;
	output_conf.pull_up_en = 0;
	gpio_config(&output_conf);
}


void Start4G()
{
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	gpio_set_level(GPIO_OUTPUT_RESET, 1);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	vTaskDelay(2000 / portTICK_PERIOD_MS);


	gpio_set_level(GPIO_OUTPUT_RESET, 0);
	vTaskDelay(10 / portTICK_PERIOD_MS);

	gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);

	vTaskDelay(200 / portTICK_PERIOD_MS);

	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);

	vTaskDelay(1000 / portTICK_PERIOD_MS);

	gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);

	vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// #define BRIDGE_CELLULAR_MODEM 1

void PlaySound()
{
	/*
	 * Prepare and set configuration of timers
	 * that will be used by LED Controller
	 */
	ledc_timer_config_t ledc_timer = {
		.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
		.freq_hz = 1000,                      // frequency of PWM signal
		.speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
		.timer_num = LEDC_TIMER_0,            // timer index
		.clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
	};
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);


	/*
	 * Prepare individual configuration
	 * for each channel of LED Controller
	 * by selecting:
	 * - controller's channel number
	 * - output duty cycle, set initially to 0
	 * - GPIO number where LED is connected to
	 * - speed mode, either high or low
	 * - timer servicing selected channel
	 *   Note: if different channels use one timer,
	 *         then frequency and bit_num of these channels
	 *         will be the same
	 */


	ledc_channel_config_t ledc_channel = {

		.gpio_num   = 2,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel    = LEDC_CHANNEL_0,
		.duty       = 0,
		.hpoint     = 0,
		.timer_sel  = LEDC_TIMER_0

	};

	ledc_channel_config(&ledc_channel);

	uint32_t duty = 0;

	duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);


	//while (1) {

		duty = 4000;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 500);
		vTaskDelay(150 / portTICK_PERIOD_MS);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1000);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1500);
		vTaskDelay(200 / portTICK_PERIOD_MS);

		duty = 0;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		//vTaskDelay(1000 / portTICK_PERIOD_MS);
	//}
}


void PlaySoundShort()
{
	/*
	 * Prepare and set configuration of timers
	 * that will be used by LED Controller
	 */
	ledc_timer_config_t ledc_timer = {
		.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
		.freq_hz = 1000,                      // frequency of PWM signal
		.speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
		.timer_num = LEDC_TIMER_0,            // timer index
		.clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
	};
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);


	/*
	 * Prepare individual configuration
	 * for each channel of LED Controller
	 * by selecting:
	 * - controller's channel number
	 * - output duty cycle, set initially to 0
	 * - GPIO number where LED is connected to
	 * - speed mode, either high or low
	 * - timer servicing selected channel
	 *   Note: if different channels use one timer,
	 *         then frequency and bit_num of these channels
	 *         will be the same
	 */


	ledc_channel_config_t ledc_channel = {

		.gpio_num   = 2,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel    = LEDC_CHANNEL_0,
		.duty       = 0,
		.hpoint     = 0,
		.timer_sel  = LEDC_TIMER_0

	};

	ledc_channel_config(&ledc_channel);

	uint32_t duty = 0;

	duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);


	//while (1) {

		duty = 4000;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 700);//500
		vTaskDelay(50 / portTICK_PERIOD_MS);

		duty = 0;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		//vTaskDelay(5000 / portTICK_PERIOD_MS);
	//}
}



void app_main(void)
{
	//First check hardware revision in order to configure io accordingly
	adc_init();

	InitGPIOs();

	ESP_LOGE(TAG, "Apollo multi-mode");

	storage_Init();


    //PlaySound();
    //PlaySoundShort();

	//Init to read device ID from EEPROM
	I2CDevicesInit();

    zaptecProtocolStart();

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
    connectivity_init();


    if((switchState == eConfig_4G) || (switchState == eConfig_4G_Post))
    {
    	ppp_task_start();
    }

    if(switchState == eConfig_4G_bridge)
	{
		hard_reset_cellular();
	}

	vTaskDelay(pdMS_TO_TICKS(3000));



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

	volatile struct DeviceInfo devInfo;
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

    if(switchState != eConfig_4G_bridge)
    {
    	sessionHandler_init();
    	diagnostics_port_init();
    }

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

