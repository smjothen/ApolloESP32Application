#ifndef _STORAGE_H_
#define _STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"


#include "DeviceInfo.h"


void storage_Init();
void storage_Init_Configuration();

void storage_Set_AuthenticationRequired(uint8_t newValue);
void storage_Set_TransmitInterval(uint32_t newValue);

void storage_Set_HmiBrightness(float newValue);
void storage_Set_CommunicationMode(uint8_t newValue);
void storage_Set_PermanentLock(uint8_t newValue);

void storage_Set_Standalone(uint8_t newValue);
void storage_Set_StandalonePhase(uint8_t newValue);
void storage_Set_StandaloneCurrent(float newValue);
void storage_Set_MaxInstallationCurrentConfig(float newValue);

void storage_Set_MaxPhases(uint8_t newValue);
void storage_Set_PhaseRotation(uint8_t newValue);


uint8_t storage_Get_AuthenticationRequired();
uint32_t storage_Get_TransmitInterval();
float storage_Get_TransmitChangeLevel();

float storage_Get_HmiBrightness();
uint8_t storage_Get_CommunicationMode();
uint8_t storage_Get_PermanentLock();


uint8_t storage_Get_Standalone();
uint8_t storage_Get_StandalonePhase();
float storage_Get_StandaloneCurrent();
float storage_Get_MaxInstallationCurrentConfig();

uint8_t storage_Get_MaxPhases();
uint8_t storage_Get_PhaseRotation();

esp_err_t storage_SaveConfiguration();
esp_err_t storage_ReadConfiguration();


esp_err_t storage_SaveFactoryTestState(uint8_t testOk);
esp_err_t storage_readFactoryTestState(uint8_t *pTestOk);

void storage_SaveWifiParameters(char *SSID, char *PSK);
esp_err_t storage_ReadWifiParameters(char *SSID, char *PSK);

esp_err_t storage_clearWifiParameters();
esp_err_t storage_clearRegistrationParameters();


#ifdef __cplusplus
}
#endif

#endif  /*_STORAGE_H_*/




