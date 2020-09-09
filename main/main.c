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
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"
#include "esp_websocket_client.h"

#include "protocol_task.h"
#include "mcu_communication.h"
#include "zaptec_protocol_serialisation.h"
#include "ppp_task.h"
//#include "at_commands.h"
//#include "mqtt_demo.h"
#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"

#include "ocpp_task.h"
//#include "CLRC661.h"

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

//#include "esp_gap_ble_api.h"
//#include "esp_gatts_api.h"
//#include "esp_bt_defs.h"
//#include "esp_bt_main.h"
//#include "esp_gatt_common_api.h"

#include "../components/ble/ble_interface.h"
//#include "esp_ble_mesh_defs.h"
//#include "../bt/esp_ble_mesh/api/esp_ble_mesh_defs.h"


// #define BRIDGE_CELLULAR_MODEM 1
// #define USE_CELLULAR_CONNECTION 1

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE

#define LEDC_TEST_CH_NUM       (1)
//#define LEDC_TEST_DUTY         (4000)
//#define LEDC_TEST_FADE_TIME    (3000)

static void obtain_time(void);
static void initialize_sntp(void);

//char softwareVersion[] = "ZAP 0.0.0.1";
static const char *TAG = "MAIN     ";

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}





void init_mcu(){
    ZapMessage txMsg;

	// ZEncodeMessageHeader* does not check the length of the buffer!
	// This should not be a problem for most usages, but make sure strings are within a range that fits!
	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];

	txMsg.type = MsgWrite;
	txMsg.identifier = ParamRunTest;

	uint encoded_length = ZEncodeMessageHeaderAndOneByte(
		&txMsg, 34, txBuf, encodedTxBuf
	);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

	ESP_LOGI(TAG, "MCU initialised");
	freeZapMessageReply();

}

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



//static xQueueHandle gpio_evt_queue = NULL;

/*static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}*/


/*static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%d] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}*/


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



//float network_WifiSignalStrength()
//{
//	wifi_ap_record_t wifidata;
//	float wifiRSSI = 0.0;
//	if (esp_wifi_sta_get_ap_info(&wifidata)==0)
//		wifiRSSI= (float)wifidata.rssi;
//
//	return wifiRSSI;
//}


void app_main(void)
{
	//First check hardware revision in order to configure io accordingly
	adc_init();

	InitGPIOs();

	ESP_LOGE(TAG, "Apollo multi-mode");


//	struct Configuration
//	{
//		bool dataStructureIsInitialized;
//		uint32_t transmitInterval;
//		float transmitChangeLevel;
//
//		uint32_t communicationMode;
//		float HmiBrightness;
//		uint32_t maxPhases;
//	};

//	struct Configuration configurationFile;
//	configurationFile.dataStructureIsInitialized = true;
//	configurationFile.transmitInterval = 60;
//	configurationFile.transmitChangeLevel = 1.0;
//	configurationFile.communicationMode = 0;
//	configurationFile.HmiBrightness = 0.50;
//	configurationFile.maxPhases = 3;
//

	//storage_Init();
	//storage_Init_Configuration();
	//esp_err_t err = storage_SaveConfiguration();
	//err = storage_ReadConfiguration();

//
//
//	volatile struct Configuration configurationFileRead;
//	volatile size_t readLength = sizeof(configurationFileRead);

//
//	readLength = readLength;
    //PlaySound();
    //PlaySoundShort();

	//Read device ID from EEPROM
	I2CDevicesInit();

    zaptecProtocolStart();

    vTaskDelay(pdMS_TO_TICKS(3000));

    enum sConfig {
    	eConfig_Wifi_Zaptec  	= 1,
		eConfig_Wifi_Hotspot 	= 2,
		eConfig_Wifi_Home_Wr32	= 3,
		eConfig_Wifi_EMC 		= 4,
		eConfig_Wifi_EMC_TCP    = 5,
		eConfig_Wifi_Post		= 6,
		eConfig_4G 				= 7,
		eConfig_4G_Post			= 8,
		eConfig_4G_bridge 		= 9
    };

    int switchState = MCU_GetSwitchState();

    while(switchState == 0)
    {
    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    	switchState = MCU_GetSwitchState();
    }

    if (switchState <= eConfig_Wifi_EMC_TCP)
    {
    	ESP_ERROR_CHECK( nvs_flash_init() );
		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK( esp_event_loop_create_default() );
		configure_wifi(switchState);
    }


    ////ocpp_task_start(); //For future use
    
    //#ifdef BRIDGE_CELLULAR_MODEM
    //hard_reset_cellular();
    //mbus_init();

    if((switchState == eConfig_4G) || (switchState == eConfig_4G_Post))
    {
    	ppp_task_start();
    }

    if(switchState == eConfig_4G_bridge)
	{
		hard_reset_cellular();
	}

	vTaskDelay(pdMS_TO_TICKS(3000));

	if((switchState != eConfig_Wifi_Home_Wr32) &&
		(switchState != eConfig_Wifi_EMC_TCP) &&
		(switchState != eConfig_4G_bridge))
	{
		obtain_time();
	}


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

	strcpy(writeDevInfo.serialNumber, "ZAP000010");
	strcpy(writeDevInfo.PSK, "rvop1J1GQMsR91puAZLuUs3nTMzf02UvNA83WDWMuz0=");
	strcpy(writeDevInfo.Pin, "6695");

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
	}

	//strcpy(devInfo.serialNumber, "ZAP000010");
	//strcpy(devInfo.PSK, "rvop1J1GQMsR91puAZLuUs3nTMzf02UvNA83WDWMuz0=");
	//strcpy(devInfo.Pin, "6695");

	ble_interface_init();
//
//	while(1)
//	{
//		vTaskDelay(3000 / portTICK_PERIOD_MS);
//		ESP_LOGE(TAG, "Apollo multi-mode");
//	}


	if(switchState != eConfig_Wifi_Home_Wr32)
		I2CDevicesStartTask();


	if((switchState == eConfig_Wifi_Zaptec) ||
	   (switchState == eConfig_Wifi_Hotspot) ||
	   (switchState == eConfig_Wifi_EMC) ||
	   (switchState == eConfig_4G))
	{
		start_cloud_listener_task(devInfo);
	}

	if((switchState == eConfig_Wifi_Post) || (switchState == eConfig_4G_Post))
	{
		SetDataInterval(10);
	}
    
	uint32_t ledState = 0;

    gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    if(switchState != eConfig_4G_bridge)
    {
    	sessionHandler_init();
    	network_init();
    }

    size_t free_heap_size_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    uint32_t counter = 0;

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
    		size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    		ESP_LOGE(TAG, "# %d:  %s , rst: %d, Heaps: %i %i, Sw: %i", counter, softwareVersion, esp_reset_reason(), free_heap_size_start, (free_heap_size_start-free_heap_size), switchState);
    	}

    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void obtain_time(void)
{
    //ESP_ERROR_CHECK( nvs_flash_init() );
	//ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK( esp_event_loop_create_default() );


    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */

    //ESP_ERROR_CHECK(example_connect());

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };

    int retry = 0;
    const int retry_count = 60;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d), status: %d", retry, retry_count, sntp_get_sync_status());
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // ESP_ERROR_CHECK( example_disconnect() );

    //now = 1596673284;
    time(&now);
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    setenv("TZ", "UTC-0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The sensible time is: %s", strftime_buf);
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    //sntp_set_sync_interval(20000);
    sntp_setservername(0, "pool.ntp.org");
    //sntp_setserver(1,"216.239.35.12");//0xD8EF230C);// 216.239.35.12)
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
}
