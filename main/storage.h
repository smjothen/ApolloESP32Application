#ifndef _STORAGE_H_
#define _STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"


#include "DeviceInfo.h"


void storage_Init();
void storage_Init_Configuration();
void storage_Initialize_ScheduleParameteres();
void storage_Initialize_UK_TestScheduleParameteres();
void storage_Initialize_NO_TestScheduleParameteres();

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

//Set OCPP settings
void storage_Set_url_ocpp(const char * newValue);
void storage_Set_chargebox_identity_ocpp(const char * newValue);
void storage_Set_availability_ocpp(bool newValue);
void storage_Set_ocpp_allow_offline_tx_for_unknown_id(bool newValue);
void storage_Set_ocpp_authorization_cache_enabled(bool newValue);
void storage_Set_ocpp_authorize_remote_tx_requests(bool newValue);
void storage_Set_ocpp_clock_aligned_data_interval(uint32_t newValue);
void storage_Set_ocpp_connection_timeout(uint32_t newValue);
void storage_Set_ocpp_heartbeat_interval(uint32_t newValue);
void storage_Set_ocpp_local_authorize_offline(bool newValue);
void storage_Set_ocpp_local_pre_authorize(bool newValue);
void storage_Set_ocpp_message_timeout(uint16_t newValue);
void storage_Set_ocpp_meter_values_aligned_data(const char * newValue);
void storage_Set_ocpp_meter_values_sampled_data(const char * newValue);
void storage_Set_ocpp_meter_value_sample_interval(uint32_t newValue);
void storage_Set_ocpp_minimum_status_duration(uint32_t newValue);
void storage_Set_ocpp_reset_retries(uint8_t newValue);
void storage_Set_ocpp_stop_transaction_on_ev_side_disconnect(bool newValue);
void storage_Set_ocpp_stop_transaction_on_invalid_id(bool newValue);
void storage_Set_ocpp_stop_txn_aligned_data(const char * newValue);
void storage_Set_ocpp_stop_txn_sampled_data(const char * newValue);
void storage_Set_ocpp_transaction_message_attempts(uint8_t newValue);
void storage_Set_ocpp_transaction_message_retry_interval(uint16_t newValue);
void storage_Set_ocpp_unlock_connector_on_ev_side_disconnect(bool newValue);
void storage_Set_ocpp_local_auth_list_enabled(bool newValue);
void storage_Set_ocpp_default_id_token(const char * newValue);

//Set Local settings
void storage_Set_session_controller(enum session_controller type);
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
void storage_Set_And_Save_DiagnosticsLog(char * newString);
void storage_Clear_And_Save_DiagnosticsLog();

void storage_Set_Location(char * newString);
void storage_Set_Timezone(char * newString);
void storage_Set_TimeSchedule(char * newString);
void storage_Set_MaxStartDelay(uint32_t newValue);

void storage_Set_cover_on_value(uint16_t newValue);

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
uint32_t storage_Get_MaxStartDelay();

//Get OCPP settings
const char * storage_Get_url_ocpp();
const char * storage_Get_chargebox_identity_ocpp();
bool storage_Get_availability_ocpp();
bool storage_Get_ocpp_allow_offline_tx_for_unknown_id();
bool storage_Get_ocpp_authorization_cache_enabled();
bool storage_Get_ocpp_authorize_remote_tx_requests();
uint32_t storage_Get_ocpp_clock_aligned_data_interval();
uint32_t storage_Get_ocpp_connection_timeout();
uint32_t storage_Get_ocpp_heartbeat_interval();
bool storage_Get_ocpp_local_authorize_offline();
bool storage_Get_ocpp_local_pre_authorize();
uint16_t storage_Get_ocpp_message_timeout();
const char * storage_Get_ocpp_meter_values_aligned_data();
const char * storage_Get_ocpp_meter_values_sampled_data();
uint32_t storage_Get_ocpp_meter_value_sample_interval();
uint32_t storage_Get_ocpp_minimum_status_duration();
uint8_t storage_Get_ocpp_reset_retries();
bool storage_Get_ocpp_stop_transaction_on_ev_side_disconnect();
bool storage_Get_ocpp_stop_transaction_on_invalid_id();
const char * storage_Get_ocpp_stop_txn_aligned_data();
const char * storage_Get_ocpp_stop_txn_sampled_data();
uint8_t storage_Get_ocpp_transaction_message_attempts();
uint16_t storage_Get_ocpp_transaction_message_retry_interval();
bool storage_Get_ocpp_unlock_connector_on_ev_side_disconnect();
bool storage_Get_ocpp_local_auth_list_enabled();
const char * storage_Get_ocpp_default_id_token();

//Get Local settings
enum session_controller storage_Get_session_controller();
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
int storage_Get_DiagnosticsLogLength();

char * storage_Get_Location();
char * storage_Get_Timezone();
char * storage_Get_TimeSchedule();

uint16_t storage_Get_cover_on_value();

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

/*esp_err_t storage_SaveSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode);
esp_err_t storage_ReadSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode);
size_t storage_CheckSessionResetFile();
esp_err_t storage_clearSessionResetInfo();*/
double storage_update_accumulated_energy(float session_energy);
int storage_clear_accumulated_energy();

esp_err_t storage_clearWifiParameters();
esp_err_t storage_clearRegistrationParameters();

void storage_GetStats();

#ifdef __cplusplus
}
#endif

#endif  /*_STORAGE_H_*/




