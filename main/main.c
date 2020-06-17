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
#include "mqtt_demo.h"
#include "zaptec_cloud_listener.h"

#include "ocpp_task.h"
#include "CLRC661.h"
#include "uart1.h"
#include "adc_control.h"
#include "driver/ledc.h"

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE

#define LEDC_TEST_CH_NUM       (1)
//#define LEDC_TEST_DUTY         (4000)
//#define LEDC_TEST_FADE_TIME    (3000)

static void obtain_time(void);
static void initialize_sntp(void);

char softwareVersion[] = "ZAP 0.0.0.1 0";
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

//LED
#define GPIO_OUTPUT_DEBUG_LED    0

//AUDIO
#define LEDC_TEST_CH_NUM_E 0
#define GPIO_OUTPUT_AUDIO   (2)

#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_DEBUG_LED | 1ULL<<GPIO_OUTPUT_AUDIO)

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
//	ledc_channel_config_t ledc_channel = {
//		{
//			.channel    = LEDC_CHANNEL_0,
//			.duty       = 0,
//			.gpio_num   = 2,
//			.speed_mode = LEDC_HIGH_SPEED_MODE,
//			.hpoint     = 0,
//			.timer_sel  = LEDC_TIMER_0
//		},
//
//	};

	ledc_channel_config_t ledc_channel = {

		.gpio_num   = 2,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel    = LEDC_CHANNEL_0,
		.duty       = 0,
		.hpoint     = 0,
		.timer_sel  = LEDC_TIMER_0

	};



	ledc_channel_config(&ledc_channel);

	bool swap = false;
	uint32_t duty = 0;

	duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);


	while (1) {

		duty = 4000;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 500);
		vTaskDelay(150 / portTICK_PERIOD_MS);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1000);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1500);
		vTaskDelay(200 / portTICK_PERIOD_MS);

		duty = 8191;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}


#define GPIO_INPUT_BUTTON    4
#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_BUTTON)

void app_main(void)
{

    ESP_LOGE(TAG, "start of app_main6");

	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	 //bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);

    gpio_config_t output_conf; 
	output_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	output_conf.mode = GPIO_MODE_OUTPUT;
	output_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	output_conf.pull_down_en = 0;
	output_conf.pull_up_en = 0;
	gpio_config(&output_conf);
    
	adc_init();
	//obtain_time();
    //vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    //obtain_time();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // PlaySound();

    //mbus_init();
    //register_i2ctools();

    //zaptecProtocolStart();
    // init_mcu();

    //ocpp_task_start();
    
    #ifdef BRIDGE_CELLULAR_MODEM
    hard_reset_cellular();
    mbus_init();
    #else
    ppp_task_start();
    #endif

    // start_mqtt_demo();
    start_cloud_listener_task();
    
	uint32_t ledState = 0;
	uint32_t loopCount = 0;

	 //gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);

//	while(true)
//	{
//		if(ledState == 0)
//			ledState = 1;
//		else
//			ledState = 0;
//
//		gpio_set_level(GPIO_OUTPUT_AUDIO, ledState);
//
//		//vTaskDelay(2 / portTICK_PERIOD_MS);
//		vTaskDelay(10);
//	}

    gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    while (true)
    {
    	if(ledState == 0)
    		ledState = 1;
    	else
    		ledState = 0;

    	gpio_set_level(GPIO_OUTPUT_DEBUG_LED, ledState);

    	vTaskDelay(1000 / portTICK_PERIOD_MS);

        //gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);

        loopCount++;
		if(loopCount == 30)
		{
			ESP_LOGE(TAG, "%s , rst: %d", softwareVersion, esp_reset_reason());
			loopCount = 0;
		}
    }
    
}

static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };

    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // ESP_ERROR_CHECK( example_disconnect() );

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
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
}
