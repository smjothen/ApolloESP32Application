/* cmd_i2ctools.c

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

//#include "argtable3/argtable3.h"
#include <time.h>
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "i2cInterface.h"
#include "SHT30.h"
#include "RTC.h"
#include "EEPROM.h"
#include "CLRC661.h"
//#include "audioBuzzer.h"
//#include "../audio/include/audioBuzzer.h"
#include "C:/gitHub/Apollo/goEsp32/ApolloESP32Application/main/audioBuzzer.h"
//#include "C:/gitHub/Apollo/goEsp32/ApolloESP32Application/components/audio/include/audioBuzzer.h"

#include "driver/ledc.h"
#include <string.h>
#include "i2cDevices.h"

static const char *TAG = "I2C-DEVICES";

static float temperature = 0.0;
static float humidity = 0.0;


//AUDIO
#define LEDC_TEST_CH_NUM_E 0
#define GPIO_OUTPUT_AUDIO   (2)
static ledc_channel_config_t ledc_channel;


struct DeviceInfo deviceInfo;

struct DeviceInfo i2cGetSerialNumber()
{
	return deviceInfo;
}


float I2CGetSHT30Temperature()
{
	return temperature;
}

float I2CGetSHT30Humidity()
{
	return humidity;
}

void audioInit()
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


		ledc_channel_config_t ledc_channel_init = {

			.gpio_num   = 2,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.channel    = LEDC_CHANNEL_0,
			.duty       = 0,
			.hpoint     = 0,
			.timer_sel  = LEDC_TIMER_0

		};

		ledc_channel = ledc_channel_init;

		ledc_channel_config(&ledc_channel);


		uint32_t duty = 0;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

}

void audio_play_nfc_card_accepted_debug()
{
	uint32_t duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

	ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 700);//500
	vTaskDelay(50 / portTICK_PERIOD_MS);

	duty = 0;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

}


static void i2cDevice_task(void *pvParameters)
{

	do_i2cdetect_cmd();
	SHT30Init();



	struct tm writeTime = {0};
	strptime("2020-06-29 11:10:01", "%Y-%m-%d %H:%M:%S", &writeTime);

	struct tm readTime = {0};

	audioInit();

	NFCInit();

	RTCWriteTime(writeTime);

	//EEPROM_Erase();

	EEPROM_Read();

	uint8_t formatVersion = 1;
	char * serialNumber = "ZAP000005";
	char * psk = "vHZdbNkcPhqJRS9pqEaokFv1CrKN1i2opy4qzikyTOM=";
	char * pin = "4284";


	uint8_t correctFormatVersion = 1;
	uint8_t readFormatVersion = 0;

	EEPROM_ReadFormatVersion(readFormatVersion);
	if(readFormatVersion == correctFormatVersion)
	{
		ESP_LOGI(TAG, "EEPROM format version %d", readFormatVersion);
	}
	else
	{
		ESP_LOGI(TAG, "No format on EEPROM!!! %d", readFormatVersion);

		EEPROM_WriteFormatVersion(formatVersion);
	}


	EEPROM_ReadSerialNumber(deviceInfo.serialNumber);
	int len = strlen(deviceInfo.serialNumber);

	//Check for valid serial number
	if((len == 9) && (deviceInfo.serialNumber[0] == 'Z') && (deviceInfo.serialNumber[1] == 'A') && (deviceInfo.serialNumber[2] == 'P'))
	{
		ESP_LOGI(TAG, "Serial number: %s", deviceInfo.serialNumber);

		EEPROM_ReadPSK(deviceInfo.PSK);
		ESP_LOGI(TAG, "PSK:           %s", deviceInfo.PSK);

		EEPROM_ReadPin(deviceInfo.Pin);
		ESP_LOGI(TAG, "PIN:           %s", deviceInfo.Pin);
	}
	//Write serial number
	else
	{
		EEPROM_WriteSerialNumber(serialNumber);
		EEPROM_WritePSK(psk);
		EEPROM_WritePin(pin);
		EEPROM_Read();
	}


	int i2cCount = 0;
	int nfcCardDetected = 0;

	while (true)
	{

		nfcCardDetected = NFCReadTag();
		if(nfcCardDetected > 0)
			audio_play_nfc_card_accepted_debug();


		i2cCount++;
		if(i2cCount >= 2)
		{
			i2cCount = 0;

			temperature = SHT30ReadTemperature();
			humidity = SHT30ReadHumidity();

			readTime = RTCReadTime();
			char timebuf[30];
			//setenv("TZ", "UTC-0", 1);
			//tzset();
			//localtime_r(&now, &timeinfo);
			//strftime(timebuf, sizeof(timebuf), "%c", &readTime);
			strftime(timebuf, sizeof(timebuf), "%F %T", &readTime);

			//ESP_LOGI(TAG, "Temp: %3.2fC Hum: %3.2f%%, Time is: %s", temperature, humidity, timebuf);

			//EEPROM_Write();
			//EEPROM_Read();
		}

		vTaskDelay(500 / portTICK_RATE_MS);
	}
}

void I2CDevicesInit()
{
	static uint8_t ucParameterToPass = {0};
	TaskHandle_t taskHandle = NULL;
	xTaskCreate( i2cDevice_task, "ocppTask", 4096, &ucParameterToPass, 5, &taskHandle );

}
