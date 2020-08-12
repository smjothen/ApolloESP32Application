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

static const char *TAG = "I2C-DEVICES";

static float temperature = 0.0;
static float humidity = 0.0;

float I2CGetSHT30Temperature()
{
	return temperature;
}

float I2CGetSHT30Humidity()
{
	return humidity;
}

static void i2cDevice_task(void *pvParameters)
{

	do_i2cdetect_cmd();
	SHT30Init();

	struct tm writeTime = {0};
	strptime("2020-06-29 11:10:01", "%Y-%m-%d %H:%M:%S", &writeTime);

	struct tm readTime = {0};

	NFCInit();

	RTCWriteTime(writeTime);
	EEPROM_Read();


	int i2cCount = 0;

	while (true)
	{

		NFCReadTag();

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

			ESP_LOGI(TAG, "Temp: %3.2fC Hum: %3.2f%%, Time is: %s", temperature, humidity, timebuf);

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
