#include <stdio.h>
#include "driver/i2c.h"
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
#include "production_test.h"
#include "../../main/connectivity.h"
#include "../../main/chargeSession.h"
#include "../../main/sessionHandler.h"
#include "../ntp/zntp.h"
#include "../zaptec_cloud/include/zaptec_cloud_listener.h"

static const char *TAG = "I2C_DEVICES    ";
static const char *TAG_EEPROM = "EEPROM STATUS  ";

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

#ifdef RUN_FACTORY_TESTS
	deviceInfo.factory_stage = FactoryStageUnknown2;
#endif
#ifdef RUN_FACTORY_ASSIGN_ID
	deviceInfo.factory_stage = FactoryStageUnknown;
	deviceInfo.EEPROMFormatVersion = 0xff;
#endif

	return deviceInfo;
}

bool i2cSerialIsZGB()
{
	if(strnstr(deviceInfo.serialNumber,"ZGB",3) != NULL)
		return true;
	else
		return false;
}

/*
 * Chargers below serial number ~ZAP000149 had a different partition table without the "files" partition.
 * This function can be used to identify chargers that MAY have the partition.
 */
bool i2cCheckSerialForDiskPartition()
{
	if(strstr(deviceInfo.serialNumber,"ZAP") != NULL) 	///ZGB should always return false since it always has the new partition table
	{
		int serial = atoi(&deviceInfo.serialNumber[3]);
		if(serial < 149)
			return true;
		else
			return false;
	}

	return false;
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

uint8_t deviceInfoVersionOnEeprom(){
	uint8_t result;
	EEPROM_ReadFormatVersion(&result);
	return result;
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
	EEPROM_ReadFactroyStage(&deviceInfo.factory_stage);
	ESP_LOGI(TAG_EEPROM, "Factory stage: %d", deviceInfo.factory_stage);

	if(deviceInfo.EEPROMFormatVersion == GetEEPROMFormatVersion())
	{
		//printf("\n********************************\n\n");
		ESP_LOGI(TAG_EEPROM, "Format ver:    %d", deviceInfo.EEPROMFormatVersion);

		EEPROM_ReadSerialNumber(deviceInfo.serialNumber);
		int len = strlen(deviceInfo.serialNumber);

		//Check for valid serial number
		if((len == 9) && ((strncmp(deviceInfo.serialNumber, "ZAP", 3) == 0) || (strncmp(deviceInfo.serialNumber, "ZGB", 3) == 0)))
		{
			ESP_LOGI(TAG_EEPROM, "Serial number: %s", deviceInfo.serialNumber);

			EEPROM_ReadPSK(deviceInfo.PSK);
			ESP_LOGI(TAG_EEPROM, "PSK:           %s", deviceInfo.PSK);

			EEPROM_ReadPin(deviceInfo.Pin);
			ESP_LOGI(TAG_EEPROM, "PIN:           %s", deviceInfo.Pin);

			//printf("\n********************************\n\n");
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

	// Protect prototype versions with serial numbers below ZAP000050 that has not been through factory test
	// from the production test. If upgraded ota without this they will go into production test mode!
	if(deviceInfo.EEPROMFormatVersion == GetEEPROMFormatVersion())
	{
		int serial = atoi(deviceInfo.serialNumber + 3);
		if (serial < 50)
		{
			deviceInfo.factory_stage = FactoryStageFinnished;
		}
	}
	deviceInfoLoaded = true;

#ifdef RUN_FACTORY_TESTS
	deviceInfo.factory_stage = FactoryStageUnknown2;
#endif
#ifdef RUN_FACTORY_ASSIGN_ID
	deviceInfo.factory_stage = FactoryStageUnknown;
	deviceInfo.EEPROMFormatVersion = 0xff;
#endif

	return deviceInfo;
}

//This function is needed to avoid conflict between NTP and RTC.
bool i2cRTCChecked()
{
	return RTCchecked;
}

uint32_t passedDetectedCounter = 0;
uint32_t failedDetectedCounter = 0;

uint32_t GetPassedDetectedCounter()
{
	return passedDetectedCounter;
}

uint32_t GetFailedDetectedCounter()
{
	return failedDetectedCounter;
}


bool RTCHasNewTime = false;
void i2cFlagNewTimeWrite()
{
	RTCHasNewTime = true;
}

uint8_t isAuthenticated = 0;
uint8_t i2cIsAuthenticated()
{
	return isAuthenticated;
}

void i2cClearAuthentication()
{
	isAuthenticated = 0;
}

static bool isNfcTagPairing;
void i2cSetNFCTagPairing(bool pairingState)
{
	isNfcTagPairing = pairingState;
}

static void i2cDevice_task(void *pvParameters)
{
	RTCVerifyControlRegisters();

	RTCReadAndUseTime();

	RTCchecked = true;

	//SHT30Init();

	audioInit();

	int i2cCount = 0;
	int nfcCardDetected = 0;

	uint8_t blockReRead = 8;

	//storage_Set_AuthenticationRequired(1);

	//Without authentication, don't initalize the NFC
	//If active at boot, or activated later, initialize the NFC antenna once
	//if((storage_Get_AuthenticationRequired() == 1) && (NFCInitialized == false))



	NFCInit();

	while (true)
	{
		// Continuously read NFC in custom modes
		if(prodtest_active() || (storage_Get_DiagnosticsMode() == eNFC_ERROR_COUNT) || (storage_Get_DiagnosticsMode() == eACTIVATE_TCP_PORT) || isNfcTagPairing)
		{
			nfcCardDetected = NFCReadTag();

		}
		else if(storage_Get_AuthenticationRequired() == 1)
		{
			//Normally don't read NFC unless a car is connected
			//if(MCU_GetchargeMode() != 12)
			//{

				if(blockReRead == 8)
				{
					nfcCardDetected = NFCReadTag();

					//Blocking is used to prevent multiple reads if user holds the chip over reader for several seconds
					if(nfcCardDetected)
						blockReRead = 7;
				}
				if(blockReRead < 8)
				{
					if(blockReRead > 0)
					{
						blockReRead--;
						ESP_LOGI(TAG, "NFC Block %i", blockReRead);
					}
					else
					{
						blockReRead = 8;
						ESP_LOGI(TAG, "NFC UnBlocking");
					}
				}
			//}

		}

		//Test function for checking successful NFC reading under certain conditions
		if(storage_Get_DiagnosticsMode() == eNFC_ERROR_COUNT)
		{
			if(nfcCardDetected == true)
				passedDetectedCounter++;
			else
				failedDetectedCounter++;
		}
		else
		{
			passedDetectedCounter = 0;
			failedDetectedCounter = 0;
		}
		//if(!nfcCardDetected)
		//	NFCClearTag();

		if((storage_Get_AuthenticationRequired() == 1) || prodtest_active())
		{
			if((nfcCardDetected > 0) && prodtest_active())
			{
				prodtest_on_nfc_read();
			}
			else if((nfcCardDetected > 0) && (MCU_GetChargeMode() == 12) && (isNfcTagPairing == false))
			{
				audio_play_single_biip();
				ESP_LOGW(TAG, "Card working, clearing...");
				NFCClearTag();
			}
			else if(nfcCardDetected > 0)
			{
				//isAuthenticated = authentication_CheckId(NFCGetTagInfo());

				ESP_LOGW(TAG, "Session: %s", chargeSession_Get().AuthenticationCode);
				ESP_LOGW(TAG, "NFC:     %s", NFCGetTagInfo().idAsString);
				ESP_LOGW(TAG, "cmp:     %i", strcmp(chargeSession_Get().AuthenticationCode, NFCGetTagInfo().idAsString));

				ESP_LOGW(TAG, "auth:    %i", isAuthenticated);
				ESP_LOGW(TAG, "online:  %i", isMqttConnected());

				if(storage_Get_Standalone() == 0)
				{
					//Charger online authentication
					//if(isMqttConnected() == true)
					if(isNfcTagPairing == false)
					{
						if(SessionHandler_IsOfflineMode() == false)
						{
							//Is there already a session
							if(chargeSession_Get().AuthenticationCode[0] == '\0')
							{
								audio_play_nfc_card_accepted();
								ESP_LOGW(TAG, "Online: Authenticate by Cloud");
							}

							/// Authenticated session ongoing
							else
							{
								/// See if we should stop due to correct RFID tag

								ESP_LOGW(TAG, "StopTag: %s", NFCGetTagInfo().idAsString);
								ESP_LOGW(TAG, "SessTag: %s", chargeSession_GetAuthenticationCode());

								if(strcmp(NFCGetTagInfo().idAsString, chargeSession_GetAuthenticationCode()) == 0)
								{
									audio_play_nfc_card_accepted();
									chargeSession_SetStoppedByRFID(true);
									sessionHandler_InitiateResetChargeSession();

									isAuthenticated = false;
								}

							}
						}
						//Charger offline authentication
						else
						{

							//Always allow charging when offline. Requires '*' to be set in tag-list
							isAuthenticated = authentication_CheckId(NFCGetTagInfo());

							if(isAuthenticated == 1)
							{
								authentication_Execute(NFCGetTagInfo().idAsString);
							}

							else
							{
								audio_play_nfc_card_denied();
								ESP_LOGE(TAG, "ESP32: NFC DENIED! - Not same charge card!");
								MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
								if(ret == MsgCommandAck)
								{
									ESP_LOGI(TAG, "MCU: NFC DENIED!");
								}
							}
						}
					}
				}

				//Standalone
				else
				{

					//Always do local authentication in standalone independently of online/offline

					//Only check if not already authenticated
					if(isAuthenticated == 0)
					{
						isAuthenticated = authentication_CheckId(NFCGetTagInfo());

						if(isAuthenticated)
						{
							ESP_LOGW(TAG, "Standalone: ESP32: NFC ACCEPTED");
							audio_play_nfc_card_accepted();
							MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
							if(ret == MsgCommandAck)
							{
								ESP_LOGI(TAG, "Standalone: MCU: NFC ACCEPTED!");
							}
						}
						else
						{
							audio_play_nfc_card_denied();
							ESP_LOGE(TAG, "Standalone: ESP32: NFC DENIED! unknown tag");
							MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
							if(ret == MsgCommandAck)
							{
								ESP_LOGI(TAG, "Standalone: MCU: NFC DENIED!");
							}
						}
					}
				}

			}

		}


		//Clear detection status before next loop
		nfcCardDetected = 0;

		i2cCount++;
		if(i2cCount >= 6)
		{
			i2cCount = 0;

			temperature = SHT30ReadTemperature();
			humidity = SHT30ReadHumidity();

			//Debug
			/*struct tm readTime = {0};
			readTime = RTCReadTime();
			char timebuf[30];
			strftime(timebuf, sizeof(timebuf), "%F %T", &readTime);
			ESP_LOGI(TAG, "Temp: %3.2fC Hum: %3.2f%%, Time is: %s", temperature, humidity, timebuf);*/

			RTCVerifyControlRegisters();
		}

		if(RTCHasNewTime)
		{
			RTCWriteTime(zntp_GetLatestNTPTime());
			char strftime_buf[64];
		    struct tm RTCtime = RTCReadTime();
		    memset(strftime_buf,0,sizeof(strftime_buf));
			strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &RTCtime);

			ESP_LOGW(TAG, "NTP synced time read from RTC: %s", strftime_buf);

			RTCHasNewTime = false;
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

	xTaskCreate( i2cDevice_task, "ocppTask", 4096, &ucParameterToPass, 5, &taskI2CHandle );

}
