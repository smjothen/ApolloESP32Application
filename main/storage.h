#ifndef _STORAGE_H_
#define _STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"


#include "DeviceInfo.h"


void storage_Init();
void storage_Init_Configuration();

//Set Cloud settings
void storage_Set_AuthenticationRequired(uint8_t newValue);
void storage_Set_CurrentInMaximum(float newValue);
void storage_Set_CurrentInMinimum(float newValue);
void storage_Set_MaxPhases(uint8_t newValue);
void storage_Set_DefaultOfflinePhase(uint8_t newValue);
void storage_Set_DefaultOfflineCurrent(float newValue);
void storage_Set_IsEnabled(uint8_t newValue);
void storage_Set_InstallationId(char * newValue);
void storage_Set_RoutingId(char * newValue);
void storage_Set_ChargerName(char * newValue);
void storage_Set_DiagnosticsMode(uint32_t newValue);
void storage_Set_TransmitInterval(uint32_t newValue);
void storage_Set_TransmitChangeLevel(float newValue);

//Set Local settings
void storage_Set_CommunicationMode(uint8_t newValue);
void storage_Set_HmiBrightness(float newValue);
void storage_Set_PermanentLock(uint8_t newValue);
void storage_Set_Standalone(uint8_t newValue);
void storage_Set_StandalonePhase(uint8_t newValue);
void storage_Set_StandaloneCurrent(float newValue);
void storage_Set_MaxInstallationCurrentConfig(float newValue);
void storage_Set_PhaseRotation(uint8_t newValue);
void storage_Set_NetworkType(uint8_t newValue);
void storage_Set_NetworkTypeOverride(uint8_t newValue);
void storage_Set_PulseInterval(uint32_t newValue);
void storage_Set_DiagnosticsLog(char * newValue);

//Get Cloud settings
uint8_t storage_Get_AuthenticationRequired();
float storage_Get_CurrentInMaximum();
float storage_Get_CurrentInMinimum();
uint8_t storage_Get_MaxPhases();
uint8_t storage_Get_DefaultOfflinePhase();
float storage_Get_DefaultOfflineCurrent();
uint8_t storage_Get_IsEnabled();
char * storage_Get_InstallationId();
char * storage_Get_RoutingId();
char * storage_Get_ChargerName();
uint32_t storage_Get_DiagnosticsMode();
uint32_t storage_Get_TransmitInterval();
float storage_Get_TransmitChangeLevel();


//Get Local settings
uint8_t storage_Get_CommunicationMode();
float storage_Get_HmiBrightness();
uint8_t storage_Get_PermanentLock();
uint8_t storage_Get_Standalone();
uint8_t storage_Get_StandalonePhase();
float storage_Get_StandaloneCurrent();
float storage_Get_MaxInstallationCurrentConfig();
uint8_t storage_Get_PhaseRotation();
uint8_t storage_Get_NetworkType();
uint8_t storage_Get_NetworkTypeOverride();
uint32_t storage_Get_PulseInterval();
char * storage_Get_DiagnosticsLog();

esp_err_t storage_SaveConfiguration();
esp_err_t storage_ReadConfiguration();

void storage_CreateRFIDbuffer();
char * storage_GetRFIDbuffer();
void storage_FreeRFIDbuffer();

void storage_Verify_AuthenticationSetting();
esp_err_t storage_updateRFIDTagsToFile(volatile struct RFIDTokens rfidTokens[], uint32_t nrOfTokens);
esp_err_t storage_lookupRFIDTagInList(char * tag, uint8_t *match);
uint32_t storage_ReadNrOfTagsOnFile();
esp_err_t storage_printRFIDTagsOnFile(bool writeToBuffer);
uint32_t storage_GetNrOfTagsCounter();
esp_err_t storage_clearAllRFIDTagsOnFile();

void storage_SaveWifiParameters(char *SSID, char *PSK);
esp_err_t storage_ReadWifiParameters(char *SSID, char *PSK);
void storage_PrintConfiguration();

esp_err_t storage_SaveSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode);
esp_err_t storage_ReadSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode);
size_t storage_CheckSessionResetFile();
esp_err_t storage_clearSessionResetInfo();
double storage_update_accumulated_energy(float session_energy);
int storage_clear_accumulated_energy();

esp_err_t storage_clearWifiParameters();
esp_err_t storage_clearRegistrationParameters();

void storage_GetStats();

#ifdef __cplusplus
}
#endif

#endif  /*_STORAGE_H_*/




