#include <stdio.h>
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <time.h>

#include "i2cInterface.h"
#include "SHT30.h"
#include "RTC.h"
#include "EEPROM.h"
#include "CLRC661.h"
#include "../audioBuzzer/audioBuzzer.h"

#include "driver/ledc.h"
#include <string.h>
#include "i2cDevices.h"
#include "../authentication/authentication.h"
#include "../../main/storage.h"
#include "../zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../zaptec_protocol/include/protocol_task.h"

static const char *TAG = "I2C_DEVICES";
static const char *TAG_EEPROM = "EEPROM STATUS";

static float temperature = 0.0;
static float humidity = 0.0;
static struct DeviceInfo deviceInfo;
static bool deviceInfoLoaded = false;
static bool RTCchecked = false;

void I2CDevicesInit()
{
	do_i2cdetect_cmd();
}

void i2c_ctrl_debug(int state)
{
	if(state == 0)
	{
		esp_log_level_set(TAG, ESP_LOG_NONE);
		esp_log_level_set(TAG_EEPROM, ESP_LOG_NONE);
	}
	else
	{
		esp_log_level_set(TAG, ESP_LOG_INFO);
		esp_log_level_set(TAG_EEPROM, ESP_LOG_INFO);
	}
}

struct DeviceInfo i2cGetLoadedDeviceInfo()
{
	return deviceInfo;
}

void i2cSetDebugDeviceInfoToMemory(struct DeviceInfo debugDevInfo)
{
	deviceInfo = debugDevInfo;
	deviceInfoLoaded = true;

	//Must be set to ensure NTP service is not waiting for this
	RTCchecked = true;
}


bool i2CDeviceInfoIsLoaded()
{
	return deviceInfoLoaded;
}

float I2CGetSHT30Temperature()
{
	return temperature;
}

float I2CGetSHT30Humidity()
{
	return humidity;
}


esp_err_t i2cWriteDeviceInfoToEEPROM(struct DeviceInfo newDeviceInfo)
{

	//Write serial number
	esp_err_t err = ESP_FAIL;
	int count = 3;
	while ((err != ESP_OK) && (count > 0))
	{
		err = EEPROM_WriteSerialNumber(newDeviceInfo.serialNumber);
		count--;
	}
	if(count == 0)
	{
		ESP_LOGE(TAG_EEPROM, "Could not write serialNumber to EEPROM: %s", deviceInfo.serialNumber);
		return ESP_FAIL;
	}

	err = ESP_FAIL;
	count = 3;
	while ((err != ESP_OK) && (count > 0))
	{
		err = EEPROM_WritePSK(newDeviceInfo.PSK);
		count--;
	}
	if(count == 0)
	{
		ESP_LOGE(TAG_EEPROM, "Could not write PSK to EEPROM: %s", deviceInfo.serialNumber);
		return ESP_FAIL;
	}

	err = ESP_FAIL;
	count = 3;
	while ((err != ESP_OK) && (count > 0))
	{
		err = EEPROM_WritePin(newDeviceInfo.Pin);
		count--;
	}
	if(count == 0)
	{
		ESP_LOGE(TAG_EEPROM, "Could not write PSK to EEPROM: %s", deviceInfo.serialNumber);
		return ESP_FAIL;
	}

	//Write this last to indicate valid EEPROM content
	err = ESP_FAIL;
	count = 3;
	while ((err != ESP_OK) && (count > 0))
	{
		err = EEPROM_WriteFormatVersion(newDeviceInfo.EEPROMFormatVersion);
		count--;
	}
	if(count == 0)
	{
		ESP_LOGE(TAG_EEPROM, "Could not write PSK to EEPROM: %s", deviceInfo.serialNumber);
		return ESP_FAIL;
	}

	//Display full EEPROM content
	EEPROM_Read();

	return ESP_OK;
}


struct DeviceInfo i2cReadDeviceInfoFromEEPROM()
{
	//Display full EEPROM content
	EEPROM_Read();

	EEPROM_ReadFormatVersion(&deviceInfo.EEPROMFormatVersion);
	if(deviceInfo.EEPROMFormatVersion == GetEEPROMFormatVersion())
	{
		printf("\n********************************\n\n");
			ESP_LOGI(TAG_EEPROM, "Format ver:    %d", deviceInfo.EEPROMFormatVersion);

		EEPROM_ReadSerialNumber(deviceInfo.serialNumber);
		int len = strlen(deviceInfo.serialNumber);

		//Check for valid serial number
		if((len == 9) && (deviceInfo.serialNumber[0] == 'Z') && (deviceInfo.serialNumber[1] == 'A') && (deviceInfo.serialNumber[2] == 'P'))
		{
			ESP_LOGI(TAG_EEPROM, "Serial number: %s", deviceInfo.serialNumber);

			EEPROM_ReadPSK(deviceInfo.PSK);
			ESP_LOGI(TAG_EEPROM, "PSK:           %s", deviceInfo.PSK);

			EEPROM_ReadPin(deviceInfo.Pin);
			ESP_LOGI(TAG_EEPROM, "PIN:           %s", deviceInfo.Pin);

			printf("\n********************************\n\n");
		}
		else
		{
			ESP_LOGE(TAG_EEPROM, "No valid serial number on EEPROM: %s", deviceInfo.serialNumber);
		}

	}
	else
	{
		ESP_LOGI(TAG_EEPROM, "No format on EEPROM!!! %d", deviceInfo.EEPROMFormatVersion);
		//Must perform factory onboarding
	}

	deviceInfoLoaded = true;

	return deviceInfo;
}

//This function is needed to avoid conflict between NTP and RTC.
bool i2cRTCChecked()
{
	return RTCchecked;
}


static void i2cDevice_task(void *pvParameters)
{
	RTCReadAndUseTime();

	RTCchecked = true;

	SHT30Init();

	audioInit();

	bool NFCInitialized = false;



	int i2cCount = 0;
	int nfcCardDetected = 0;

	uint8_t isAuthenticated = 0;

	while (true)
	{
		storage_Set_AuthenticationRequired(1);

		//Without authentication, don't initalize the NFC
		//If active at boot, or activated later, initialize the NFC antenna once
		if((storage_Get_AuthenticationRequired() == 1) && (NFCInitialized == false))
		{
			NFCInit();
			NFCInitialized = true;
		}

		if((storage_Get_AuthenticationRequired() == 1) && (NFCInitialized == true))
		{
			nfcCardDetected = NFCReadTag();

			if(nfcCardDetected > 0)
			{
				isAuthenticated = authentication_CheckId(NFCGetTagInfo());

				if(isAuthenticated == 1)
				{
					audio_play_nfc_card_accepted_debug();
					ESP_LOGI(TAG, "EPS32: NFC ACCEPTED!");
					MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
					if(ret == MsgCommandAck)
					{
						ESP_LOGI(TAG, "MCU: NFC ACCEPTED!");
					}
				}
				else
				{

					audio_play_nfc_card_denied();
					ESP_LOGE(TAG, "ESP32: NFC DENIED!");
					MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
					if(ret == MsgCommandAck)
					{
						ESP_LOGI(TAG, "MCU: NFC DENIED!");
					}
				}
			}
		}

		i2cCount++;
		if(i2cCount >= 6)
		{
			i2cCount = 0;

			temperature = SHT30ReadTemperature();
			humidity = SHT30ReadHumidity();

			//Debug
			struct tm readTime = {0};
			readTime = RTCReadTime();
			char timebuf[30];
			strftime(timebuf, sizeof(timebuf), "%F %T", &readTime);
			ESP_LOGI(TAG, "Temp: %3.2fC Hum: %3.2f%%, Time is: %s", temperature, humidity, timebuf);
		}

		//Read from NFC at 2Hz for user to not notice delay
		vTaskDelay(500 / portTICK_RATE_MS);
	}
}

static TaskHandle_t taskI2CHandle = NULL;
int I2CGetStackWatermark()
{
	if(taskI2CHandle != NULL)
		return uxTaskGetStackHighWaterMark(taskI2CHandle);
	else
		return -1;
}


void I2CDevicesStartTask()
{
	static uint8_t ucParameterToPass = {0};

	xTaskCreate( i2cDevice_task, "ocppTask", 3072, &ucParameterToPass, 5, &taskI2CHandle );

}
