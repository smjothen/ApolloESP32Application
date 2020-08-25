#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "zaptec_protocol_serialisation.h"

static const char *TAG = "STORAGE:";

nvs_handle factory_handle;

#define CONFIG_FILE "CONFIG_FILE"
nvs_handle configuration_handle;

nvs_handle wifi_handle;
nvs_handle storage_handle;
nvs_handle scaling_handle;
esp_err_t err;



static struct Configuration configurationStruct;


void storage_Init()
{
	 esp_err_t err = nvs_flash_init();
	    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	        // NVS partition was truncated and needs to be erased
	        // Retry nvs_flash_init
	        ESP_ERROR_CHECK(nvs_flash_erase());
	        err = nvs_flash_init();
	    }
	    ESP_ERROR_CHECK( err );

	    //err = print_what_saved();
	    //if (err != ESP_OK)
	    	//printf("Error (%s) reading data from NVS!\n", esp_err_to_name(err));

#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

//	esp_err_t ret = nvs_flash_init();
//	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//	  ESP_ERROR_CHECK(nvs_flash_erase());
//	  ret = nvs_flash_init();
//	}
//	ESP_ERROR_CHECK(ret);

}

void storage_Init_Configuration()
{
	configurationStruct.dataStructureIsInitialized = false;
	configurationStruct.authenticationRequired = false;
	configurationStruct.transmitInterval = 60;
	configurationStruct.transmitChangeLevel = 1.0;
	configurationStruct.communicationMode = 0;
	configurationStruct.hmiBrightness = 0.5;
	configurationStruct.maxPhases = 3;
}

struct Configuration storage_GetConfigurationParameers()
{
	return configurationStruct;
}




void Storage_Set_AuthenticationRequired(bool newValue)
{
	configurationStruct.authenticationRequired = newValue;
}

void Storage_Set_TransmitInterval(uint32_t newValue)
{
	configurationStruct.transmitInterval = newValue;
}

void Storage_Set_HmiBrightness(float newValue)
{
	configurationStruct.hmiBrightness = newValue;
}

void Storage_Set_CommunicationMode(uint32_t newValue)
{
	configurationStruct.communicationMode = newValue;
}

void Storage_Set_MaxPhases(uint32_t newValue)
{
	configurationStruct.maxPhases = newValue;
}



bool Storage_Get_AuthenticationRequired()
{
	return configurationStruct.authenticationRequired;
}

uint32_t Storage_Get_TransmitInterval()
{
	return configurationStruct.transmitInterval;
}

void Storage_Get_HmiBrightness(float newValue)
{
	configurationStruct.hmiBrightness = newValue;
}

uint32_t Storage_Get_CommunicationMode()
{
	return configurationStruct.communicationMode;
}

uint32_t Storage_Get_MaxPhases()
{
	return configurationStruct.maxPhases;
}




esp_err_t nvs_set_zfloat(nvs_handle_t handle, const char* key, float inputValue)
{
	uint32_t floatToInt;
	memcpy(&floatToInt, &inputValue, 4);
	err = nvs_set_u32(handle, key, floatToInt);
	return err;
}

esp_err_t nvs_get_zfloat(nvs_handle_t handle, const char* key, float * outputValue)
{
	uint32_t intToFloat;
	err = nvs_get_u32(handle, key, &intToFloat);
	memcpy(&outputValue, &intToFloat, 4);

	return err;
}

esp_err_t storage_SaveConfiguration()
{
	volatile esp_err_t err;

	err = nvs_open(CONFIG_FILE, NVS_READWRITE, &configuration_handle);

	//TEMPLATES
	//err += nvs_set_blob(configuration_handle, "configuration", value, length);
	//err += nvs_set_str(configuration_handle, "ParamName", char);
	//err += nvs_set_u32(configuration_handle, "ParamName", char);
	//err += nvs_set_zfloat(configuration_handle, "ParamHmiBrightness", configurationStruct.HmiBrightness);

	err += nvs_set_u8(configuration_handle, "AuthRequired", (uint8_t)configurationStruct.authenticationRequired);
	err += nvs_set_u32(configuration_handle, "TransmitInterval", configurationStruct.transmitInterval);
	err += nvs_set_zfloat(configuration_handle, "ParamHmiBrightness", configurationStruct.hmiBrightness);

	err += nvs_set_u32(configuration_handle, "CommunicationMode", configurationStruct.communicationMode);
	err += nvs_set_u32(configuration_handle, "MaxPhases", configurationStruct.maxPhases);

	err += nvs_commit(configuration_handle);
	nvs_close(configuration_handle);
	return err;
}


esp_err_t storage_ReadConfiguration()
{
	esp_err_t err;
	err = nvs_open(CONFIG_FILE, NVS_READONLY, &configuration_handle);
	//volatile size_t requiredSize= 0;

//	err += nvs_get_blob(configuration_handle, "ConfigStruct", NULL, &requiredSize);
//	if(requiredSize != readLength)
//	{
//		nvs_close(configuration_handle);
//		return err;
//	}

	//err += nvs_get_blob(configuration_handle, "ConfigStruct", value, &readLength);
	//err += nvs_get_u8(factory_handle, "TestOk", pTestOk);
	err += nvs_get_u8(configuration_handle, "AuthRequired", (uint8_t*)&configurationStruct.authenticationRequired);
	err += nvs_get_u32(configuration_handle, "TransmitInterval", &configurationStruct.transmitInterval);
	err += nvs_get_zfloat(configuration_handle, "ParamHmiBrightness", &configurationStruct.hmiBrightness);

	err += nvs_get_u32(configuration_handle, "CommunicationMode", &configurationStruct.communicationMode);
	err += nvs_get_u32(configuration_handle, "MaxPhases", &configurationStruct.maxPhases);

	nvs_close(configuration_handle);

	return err;
}



esp_err_t storage_SaveFactoryParameters(char *apmId, char *apmPsk, char * pin, uint8_t testOk)
{
	err = nvs_open("factory", NVS_READWRITE, &factory_handle);

	err += nvs_set_str(factory_handle, "Apm_id", apmId);
	err += nvs_set_str(factory_handle, "Apm_psk", apmPsk);
	err += nvs_set_str(factory_handle, "Pin", pin);
	err += nvs_set_u8(factory_handle, "TestOk", testOk);

	err = nvs_commit(factory_handle);
	nvs_close(factory_handle);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Saved: id: %s ps: %s pin: %s, testOk: %d",apmId, apmPsk, pin, testOk);
	else
		ESP_LOGI(TAG, "ERROR %d when saving id: %s ps: %s pin: %s, testOk: %d",err, apmId, apmPsk, pin, testOk);

	return err;
}

esp_err_t storage_SaveFactoryTestState(uint8_t testOk)
{
	err = nvs_open("factory", NVS_READWRITE, &factory_handle);
	err += nvs_set_u8(factory_handle, "TestOk", testOk);
	err = nvs_commit(factory_handle);
	nvs_close(factory_handle);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Saved: testOk: %d", testOk);
	else
		ESP_LOGI(TAG, "ERROR %d when saving: testOk: %d",err, testOk);

	return err;
}



size_t storage_readFactoryUniqueId(char *uniqueId)
{
	size_t readSize = 0;
	err = nvs_open("factory", NVS_READONLY, &factory_handle);
	if(err != ESP_OK)
	{
		nvs_close(factory_handle);
		return -1;
	}

	err = nvs_get_str(factory_handle, "Apm_id", NULL, &readSize);
	if(err != ESP_OK)
	{
		nvs_close(factory_handle);
		return -1;
	}
	if(readSize > 0)
		nvs_get_str(factory_handle, "Apm_id", uniqueId, &readSize);

	nvs_close(factory_handle);
	return readSize;
}

size_t storage_readFactoryPsk(char *psk)
{
	size_t readSize;
	err = nvs_open("factory", NVS_READONLY, &factory_handle);
	nvs_get_str(factory_handle, "Apm_psk", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(factory_handle, "Apm_psk", psk, &readSize);

	nvs_close(factory_handle);
	return readSize;
}

size_t storage_readFactoryPin(char *pin)
{
	size_t readSize;
	err = nvs_open("factory", NVS_READONLY, &factory_handle);
	nvs_get_str(factory_handle, "Pin", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(factory_handle, "Pin", pin, &readSize);

	nvs_close(factory_handle);
	return readSize;
}

esp_err_t storage_readFactoryTestState(uint8_t *pTestOk)
{
	err = nvs_open("factory", NVS_READONLY, &factory_handle);
	err += nvs_get_u8(factory_handle, "TestOk", pTestOk);

	nvs_close(factory_handle);
	return err;
}


esp_err_t storage_clearFactoryParameters()
{
	err = nvs_open("factory", NVS_READWRITE, &factory_handle);
	err += nvs_erase_key(factory_handle, "Apm_id");
	err += nvs_erase_key(factory_handle, "Apm_psk");
	err += nvs_erase_key(factory_handle, "Pin");
	err += nvs_commit(factory_handle);
	nvs_close(factory_handle);

	return err;
}



/*
 * Wifi parameters
 */
void storage_SaveWifiParameters(char *SSID, char *PSK)
{
	err = nvs_open("wifi", NVS_READWRITE, &wifi_handle);

	err += nvs_set_str(wifi_handle, "WifiSSID", SSID);
	err += nvs_set_str(wifi_handle, "WifiPSK", PSK);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Saved Wifi: SSID: %s PSK: %s successfully", SSID, PSK);
	else
		ESP_LOGI(TAG, "ERROR saving Wifi: SSID: %s PSK: %s", SSID, PSK);

	err = nvs_commit(wifi_handle);
	nvs_close(wifi_handle);

}


esp_err_t storage_ReadWifiParameters(char *SSID, char *PSK)
{
	size_t readSize;

	err = nvs_open("wifi", NVS_READONLY, &wifi_handle);

	err += nvs_get_str(wifi_handle, "WifiSSID", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiSSID", SSID, &readSize);

	err += nvs_get_str(wifi_handle, "WifiPSK", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiPSK", PSK, &readSize);

	ESP_LOGI(TAG, "Storage read Wifi: SSID: %s PSK: %s",SSID, PSK);

	nvs_close(wifi_handle);

	return err;
}


esp_err_t storage_clearWifiParameters()
{
	err = nvs_open("wifi", NVS_READWRITE, &wifi_handle);
	err += nvs_erase_all(wifi_handle);
	err += nvs_commit(wifi_handle);
	nvs_close(wifi_handle);

	return err;
}

esp_err_t storage_clearRegistrationParameters()
{
	err = nvs_open("storage", NVS_READWRITE, &storage_handle);
	err += nvs_erase_key(storage_handle, "Secret");
	//err += nvs_erase_key(storage_handle, "UniqueId");
	err += nvs_commit(storage_handle);
	nvs_close(storage_handle);

	return err;
}


/*
 * Registration parameters
 */
/*size_t storage_readUniqueId(char *uniqueId)
{
	size_t readSize;
	err = nvs_open("storage", NVS_READONLY, &storage_handle);
	nvs_get_str(storage_handle, "UniqueId", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(storage_handle, "UniqueId", uniqueId, &readSize);

	nvs_close(storage_handle);
	return readSize;
}*/


size_t storage_readSecret(char *secret)
{
	size_t readSize;
	err = nvs_open("storage", NVS_READONLY, &storage_handle);
	nvs_get_str(storage_handle, "Secret", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(storage_handle, "Secret", secret, &readSize);

	nvs_close(storage_handle);
	return readSize;
}


/*void storage_saveUniqueIdParameter(char* uniqueId)
{
	err = nvs_open("storage", NVS_READWRITE, &storage_handle);

	//APMxxxxxx
	nvs_set_str(storage_handle, "UniqueId", uniqueId);

	err = nvs_commit(storage_handle);
	nvs_close(storage_handle);
}*/

void storage_saveSecretParameter(char *secret)
{
	err = nvs_open("storage", NVS_READWRITE, &storage_handle);

	//Secret key
	nvs_set_str(storage_handle, "Secret", secret);

	err = nvs_commit(storage_handle);
	nvs_close(storage_handle);
}




void storage_readRegistrationParameters(char* uniqueId, char *secret)
{
	err = nvs_open("storage", NVS_READONLY, &storage_handle);

	size_t readSize;
	//APMxxxxxx

	nvs_get_str(storage_handle, "UniqueId", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(storage_handle, "UniqueId", uniqueId, &readSize);

	ESP_LOGI(TAG, "UniqueId: %s rs: %d Len: %d",uniqueId, readSize, strlen(uniqueId));

	//Secret key
	nvs_get_str(storage_handle, "Secret", NULL, &readSize);
	if(readSize > 0)
		nvs_get_str(storage_handle, "Secret", secret, &readSize);

	ESP_LOGI(TAG, "UniqueId: %s rs: %d Len: %d",uniqueId, readSize, strlen(uniqueId));

	nvs_close(storage_handle);
}


/*
 * Control parameters
 */
void storage_SaveControlParameters(unsigned int transmitInterval, float transmitThreshold, float maxCurrent, unsigned int average)
{

	unsigned int ftiThreshold;
	memcpy(&ftiThreshold, &transmitThreshold, 4);

	unsigned int ftiMaxCurrent;
	memcpy(&ftiMaxCurrent, &maxCurrent, 4);

	err = nvs_open("storage", NVS_READWRITE, &storage_handle);

	ESP_LOGI(TAG, "Saving: TI: %d TT: %.2f MC: %.2f A: %d",transmitInterval, transmitThreshold, maxCurrent, average);

	nvs_set_u32(storage_handle, "TransmitIntval", transmitInterval);
	nvs_set_u32(storage_handle, "TransmitThrhld", ftiThreshold);
	nvs_set_u32(storage_handle, "MaxCurrent", ftiMaxCurrent);
	nvs_set_u32(storage_handle, "Average", average);

	err = nvs_commit(storage_handle);
	nvs_close(storage_handle);

}


esp_err_t storage_ReadControlParameters(unsigned int *pTransmitInterval, float *pTransmitThreshold, float *pMaxCurrent, unsigned int *pAverage)
{
	unsigned int itfThreshold;
	unsigned int itfMaxCurrent;

	err = nvs_open("storage", NVS_READONLY, &storage_handle);

	err = nvs_get_u32(storage_handle, "TransmitIntval", pTransmitInterval);
	err += nvs_get_u32(storage_handle, "TransmitThrhld", &itfThreshold);
	err += nvs_get_u32(storage_handle, "MaxCurrent", &itfMaxCurrent);
	err += nvs_get_u32(storage_handle, "Average", pAverage);

	memcpy(pTransmitThreshold, &itfThreshold, 4);
	memcpy(pMaxCurrent, &itfMaxCurrent, 4);

	ESP_LOGI(TAG, "Read: TI: %d TT: %.2f MC: %.2f A: %d", *pTransmitInterval, *pTransmitThreshold, *pMaxCurrent, *pAverage);

	nvs_close(storage_handle);

	return err;
}


void storage_SaveScalingFactor(float scalingFactor)
{

	unsigned int ftiScalingFactor;
	memcpy(&ftiScalingFactor, &scalingFactor, 4);

	err = nvs_open("scaling", NVS_READWRITE, &scaling_handle);

	ESP_LOGI(TAG, "Saving: SF: %.1f", scalingFactor);

	nvs_set_u32(scaling_handle, "ScalingFactor", ftiScalingFactor);

	err = nvs_commit(scaling_handle);
	nvs_close(scaling_handle);
}


esp_err_t storage_ReadScalingFactor(float *pScalingFactor)
{
	unsigned int itfScalingFactor;

	err = nvs_open("scaling", NVS_READONLY, &scaling_handle);

	err += nvs_get_u32(scaling_handle, "ScalingFactor", &itfScalingFactor);

	memcpy(pScalingFactor, &itfScalingFactor, 4);

	ESP_LOGI(TAG, "Read: SC: %.1f", *pScalingFactor);

	nvs_close(scaling_handle);

	return err;
}
