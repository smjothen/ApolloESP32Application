#ifndef _STORAGE_H_
#define _STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"


struct Configuration
{
	bool dataStructureIsInitialized;
	bool authenticationRequired;
	uint32_t transmitInterval;
	float transmitChangeLevel;

	uint32_t communicationMode;
	float hmiBrightness;
	uint32_t maxPhases;
};


void storage_Init();
void storage_Init_Configuration();
void Storage_Set_AuthenticationRequired(bool newValue);
void Storage_Set_TransmitInterval(uint32_t newValue);
void Storage_Set_HmiBrightness(float newValue);
void Storage_Set_CommunicationMode(uint32_t newValue);
void Storage_Set_MaxPhases(uint32_t newValue);

bool Storage_Get_AuthenticationRequired();
uint32_t Storage_Get_TransmitInterval();
void Storage_Get_HmiBrightness(float newValue);
uint32_t Storage_Get_CommunicationMode();
uint32_t Storage_Get_MaxPhases();


esp_err_t storage_SaveConfiguration();
esp_err_t storage_ReadConfiguration();

esp_err_t storage_SaveFactoryParameters(char *apmId, char *apmPsk, char * pin, uint8_t testOk);
esp_err_t storage_SaveFactoryTestState(uint8_t testOk);
size_t storage_readFactoryUniqueId(char *uniqueId);
size_t storage_readFactoryPsk(char *psk);
size_t storage_readFactoryPin(char *pin);
esp_err_t storage_readFactoryTestState(uint8_t *pTestOk);
esp_err_t storage_clearFactoryParameters();

void storage_SaveWifiParameters(char *SSID, char *PSK);
esp_err_t storage_ReadWifiParameters(char *SSID, char *PSK);

esp_err_t storage_clearWifiParameters();
esp_err_t storage_clearRegistrationParameters();

size_t storage_readUniqueId(char *uniqueId);
size_t storage_readSecret(char *secret);
void storage_saveUniqueIdParameter(char *uniqueId);
void storage_saveSecretParameter(char *secret);
void storage_seadRegistrationParameters(char *uniqueId, char *secret);

void storage_SaveControlParameters(unsigned int transmitInterval, float transmitThreshold, float maxCurrent, unsigned int average);
esp_err_t storage_ReadControlParameters(unsigned int *pTransmitInterval, float *pTransmitThreshold, float *pMaxCurrent, unsigned int *pAverage);

void storage_SaveScalingFactor(float scalingFactor);
esp_err_t storage_ReadScalingFactor(float *pScalingFactor);

#ifdef __cplusplus
}
#endif

#endif  /*_STORAGE_H_*/




