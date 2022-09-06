#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "i2cDevices.h"

#include "zaptec_protocol_serialisation.h"
#include "DeviceInfo.h"
#include "protocol_task.h"

static const char *TAG = "STORAGE        ";

#define CONFIG_FILE "CONFIG_FILE"
#define DEFAULT_TRANSMIT_INTERVAL 3600
nvs_handle configuration_handle;

// "wifi"
nvs_handle wifi_handle;

#define CS_RESET_FILE "CS_RESET_FILE"
nvs_handle session_reset_handle;

nvs_handle rfid_tag_handle;

esp_err_t err;

static struct Configuration configurationStruct;


void storage_Init()
{
	esp_err_t err = nvs_flash_init();

	/// For debug
	//err = nvs_flash_erase();
	//err = nvs_flash_init();

	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
		SetEspNotification(eNOTIFICATION_NVS_ERROR);
	}
	//ESP_ERROR_CHECK( err );

/*#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif*/

}

void storage_Init_Configuration()
{
	configurationStruct.saveCounter 				= 1;

	// Cloud settings
	configurationStruct.authenticationRequired		= 0;
	configurationStruct.currentInMaximum 			= 32.0;
	configurationStruct.currentInMinimum			= 6.0;
	configurationStruct.maxPhases 					= 3;
	configurationStruct.defaultOfflinePhase			= 1;
	configurationStruct.defaultOfflineCurrent		= 10.0;
	configurationStruct.isEnabled					= 1;

	strcpy(configurationStruct.installationId,"00000000-0000-0000-0000-000000000000");
	strcpy(configurationStruct.routingId, "default");

	memset(configurationStruct.chargerName, 0, sizeof(DEFAULT_STR_SIZE));

	configurationStruct.transmitInterval 			= DEFAULT_TRANSMIT_INTERVAL;
	configurationStruct.transmitChangeLevel 		= 1.0;
	configurationStruct.diagnosticsMode				= 0;

	// ocpp settings
	strcpy(configurationStruct.url_ocpp, "");
	configurationStruct.session_controller = eSESSION_ZAPTEC_CLOUD;

	// ocpp core profile settings
	configurationStruct.ocpp_authorize_remote_tx_requests = true;
	configurationStruct.ocpp_clock_aligned_data_interval = 0;
	configurationStruct.ocpp_connection_timeout = 60;
	configurationStruct.ocpp_connector_phase_rotation_max_length = 2;
	configurationStruct.ocpp_get_configuration_max_keys = 34;
	configurationStruct.ocpp_heartbeat_interval = 86400;
	configurationStruct.ocpp_local_authorize_offline = true;
	configurationStruct.ocpp_local_pre_authorize = false;
	strcpy(configurationStruct.ocpp_meter_values_aligned_data, "");
	configurationStruct.ocpp_meter_values_aligned_data_max_length = DEFAULT_CSL_LENGTH;
	strcpy(configurationStruct.ocpp_meter_values_sampled_data, "Energy.Active.Import.Register");
	configurationStruct.ocpp_meter_values_sampled_data_max_length = DEFAULT_CSL_LENGTH;
	configurationStruct.ocpp_meter_value_sample_interval = 0;
	configurationStruct.ocpp_number_of_connectors = 1;
	configurationStruct.ocpp_reset_retries = 0;
	configurationStruct.ocpp_stop_transaction_on_ev_side_disconnect = true;
	configurationStruct.ocpp_stop_transaction_on_invalid_id = true;
	strcpy(configurationStruct.ocpp_stop_txn_aligned_data, "");
	configurationStruct.ocpp_stop_txn_aligned_data_max_length = DEFAULT_CSL_LENGTH;
	strcpy(configurationStruct.ocpp_stop_txn_sampled_data, "");
	configurationStruct.ocpp_stop_txn_sampled_data_max_length = DEFAULT_CSL_LENGTH;
	strcpy(configurationStruct.ocpp_supported_feature_profiles, "Core,Local Auth List Management,Remote Trigger");
	configurationStruct.ocpp_supported_feature_profiles_max_length = 1;
	configurationStruct.ocpp_transaction_message_attempts = 3;
	configurationStruct.ocpp_transaction_message_retry_interval = 60;
	configurationStruct.ocpp_unlock_connector_on_ev_side_disconnect = false;

	// ocpp local auth list profile settings
	configurationStruct.ocpp_local_auth_list_enabled = true;
	configurationStruct.ocpp_local_auth_max_length = UINT8_MAX * 4;
	configurationStruct.ocpp_send_local_list_max_length = UINT8_MAX;

	//Local settings

	configurationStruct.communicationMode 			= eCONNECTION_LTE;
	configurationStruct.hmiBrightness 				= 0.7;	//0.0-1.0
	configurationStruct.permanentLock 				= 0;	//0/1

	configurationStruct.standalone 					= 1;	//0/1
	configurationStruct.standalonePhase 			= 0;	//Nr
	configurationStruct.standaloneCurrent			= 6;	//A
	configurationStruct.maxInstallationCurrentConfig = 0.0;

	configurationStruct.phaseRotation				= 0;
	configurationStruct.networkType					= 0;
	configurationStruct.networkTypeOverride			= 0;
	configurationStruct.pulseInterval				= 60;

	memset(configurationStruct.diagnosticsLog, 0, sizeof(DIAGNOSTICS_STRING_SIZE));
}


struct Configuration storage_GetConfigurationParameers()
{
	return configurationStruct;
}


void storage_Set_AuthenticationRequired(uint8_t newValue)
{
	configurationStruct.authenticationRequired = newValue;
}

void storage_Set_CurrentInMaximum(float newValue)
{
	configurationStruct.currentInMaximum = newValue;
}

void storage_Set_CurrentInMinimum(float newValue)
{
	configurationStruct.currentInMinimum = newValue;
}

void storage_Set_MaxPhases(uint8_t newValue)
{
	configurationStruct.maxPhases = newValue;
}

void storage_Set_DefaultOfflinePhase(uint8_t newValue)
{
	configurationStruct.defaultOfflinePhase = newValue;
}

void storage_Set_DefaultOfflineCurrent(float newValue)
{
	configurationStruct.defaultOfflineCurrent = newValue;
}

void storage_Set_IsEnabled(uint8_t newValue)
{
	configurationStruct.isEnabled = newValue;
}

//Max string length 37 characters
void storage_Set_InstallationId(char * newValue)
{
	strcpy(configurationStruct.installationId, newValue);
}

//Max string length 37 characters
void storage_Set_RoutingId(char * newValue)
{
	strcpy(configurationStruct.routingId, newValue);
}

//Max string length 37 characters
void storage_Set_ChargerName(char * newValue)
{
	strcpy(configurationStruct.chargerName, newValue);
}

void storage_Set_DiagnosticsMode(uint32_t newValue)
{
	configurationStruct.diagnosticsMode = newValue;
}

void storage_Set_TransmitInterval(uint32_t newValue)
{
	configurationStruct.transmitInterval = newValue;
}

void storage_Set_TransmitChangeLevel(float newValue)
{
	configurationStruct.transmitChangeLevel = newValue;
}

// Ocpp settings
void storage_Set_url_ocpp(const char * newValue)
{
	strcpy(configurationStruct.url_ocpp, newValue);
}

void storage_Set_session_controller(enum session_controller newValue)
{
	configurationStruct.session_controller = newValue;

	storage_Set_Standalone((newValue & eCONTROLLER_ESP_STANDALONE) ? 1 : 0);
}

void storage_Set_ocpp_authorize_remote_tx_requests(bool newValue)
{
	configurationStruct.ocpp_authorize_remote_tx_requests = newValue;
}

void storage_Set_ocpp_clock_aligned_data_interval(uint32_t newValue)
{
	configurationStruct.ocpp_clock_aligned_data_interval = newValue;
}

void storage_Set_ocpp_connection_timeout(uint32_t newValue)
{
	configurationStruct.ocpp_connection_timeout = newValue;
}

void storage_Set_ocpp_heartbeat_interval(uint32_t newValue)
{
	configurationStruct.ocpp_heartbeat_interval = newValue;
}

void storage_Set_ocpp_local_authorize_offline(bool newValue)
{
	configurationStruct.ocpp_local_authorize_offline = newValue;
}

void storage_Set_ocpp_local_pre_authorize(bool newValue)
{
	configurationStruct.ocpp_local_pre_authorize = newValue;
}

void storage_Set_ocpp_meter_values_aligned_data(const char * newValue)
{
	strcpy(configurationStruct.ocpp_meter_values_aligned_data, newValue);
}

void storage_Set_ocpp_meter_values_sampled_data(const char * newValue)
{
	strcpy(configurationStruct.ocpp_meter_values_sampled_data, newValue);
}

void storage_Set_ocpp_meter_value_sample_interval(uint32_t newValue)
{
	configurationStruct.ocpp_meter_value_sample_interval = newValue;
}

void storage_Set_ocpp_reset_retries(uint8_t newValue)
{
	configurationStruct.ocpp_reset_retries = newValue;
}

void storage_Set_ocpp_stop_transaction_on_ev_side_disconnect(bool newValue)
{
	configurationStruct.ocpp_stop_transaction_on_ev_side_disconnect = newValue;
}

void storage_Set_ocpp_stop_transaction_on_invalid_id(bool newValue)
{
	configurationStruct.ocpp_stop_transaction_on_invalid_id = newValue;
}

void storage_Set_ocpp_stop_txn_aligned_data(const char * newValue)
{
	strcpy(configurationStruct.ocpp_stop_txn_aligned_data, newValue);
}

void storage_Set_ocpp_stop_txn_sampled_data(const char * newValue)
{
	strcpy(configurationStruct.ocpp_stop_txn_sampled_data, newValue);
}

void storage_Set_ocpp_transaction_message_attempts(uint8_t newValue)
{
	configurationStruct.ocpp_transaction_message_attempts = newValue;
}

void storage_Set_ocpp_transaction_message_retry_interval(uint16_t newValue)
{
	configurationStruct.ocpp_transaction_message_retry_interval = newValue;
}

void storage_Set_ocpp_unlock_connector_on_ev_side_disconnect(bool newValue)
{
	configurationStruct.ocpp_unlock_connector_on_ev_side_disconnect = newValue;
}

void storage_Set_ocpp_local_auth_list_enabled(bool newValue)
{
	configurationStruct.ocpp_local_auth_list_enabled = newValue;
}

//Local settings


void storage_Set_CommunicationMode(uint8_t newValue)
{
	configurationStruct.communicationMode = newValue;
}

void storage_Set_HmiBrightness(float newValue)
{
	configurationStruct.hmiBrightness = newValue;
}

void storage_Set_PermanentLock(uint8_t newValue)
{
	configurationStruct.permanentLock = newValue;
}

void storage_Set_Standalone(uint8_t newValue)
{
	configurationStruct.standalone = newValue;

	if(newValue == 1){
		configurationStruct.session_controller |= eCONTROLLER_ESP_STANDALONE;
	}else{
		configurationStruct.session_controller &= ~eCONTROLLER_ESP_STANDALONE;
	}
}

void storage_Set_StandalonePhase(uint8_t newValue)
{
	configurationStruct.standalonePhase = newValue;
}

void storage_Set_StandaloneCurrent(float newValue)
{
	configurationStruct.standaloneCurrent = newValue;
}

void storage_Set_MaxInstallationCurrentConfig(float newValue)
{
	configurationStruct.maxInstallationCurrentConfig = newValue;
}


void storage_Set_PhaseRotation(uint8_t newValue)
{
	configurationStruct.phaseRotation = newValue;
}

void storage_Set_NetworkType(uint8_t newValue)
{
	configurationStruct.networkType = newValue;
}

/*void storage_Set_NetworkTypeOverride(uint8_t newValue)
{
	configurationStruct.networkTypeOverride = newValue;
}*/

void storage_Set_PulseInterval(uint32_t newValue)
{
	configurationStruct.pulseInterval = newValue;
}


//Max string length 37 characters
void storage_Set_And_Save_DiagnosticsLog(char * newString)
{
	if(configurationStruct.diagnosticsLog[0] == '\0')
	{
		if(strlen(newString) < DIAGNOSTICS_STRING_SIZE)
		{
			strcpy(configurationStruct.diagnosticsLog, newString);
			storage_SaveConfiguration();
			ESP_LOGW(TAG, "Saved diagnosticslog");
			return;
		}
	}
	ESP_LOGE(TAG, "Could not save to diagnosticslog");
}


void storage_Clear_And_Save_DiagnosticsLog()
{
	memset(configurationStruct.diagnosticsLog, 0, sizeof(DIAGNOSTICS_STRING_SIZE));
	storage_SaveConfiguration();

	ESP_LOGW(TAG, "Cleared diagnosticslog");
}


//****************************************************

uint8_t storage_Get_AuthenticationRequired()
{
	return configurationStruct.authenticationRequired;
}

float storage_Get_CurrentInMaximum()
{
	return configurationStruct.currentInMaximum;
}

float storage_Get_CurrentInMinimum()
{
	return configurationStruct.currentInMinimum;
}

uint8_t storage_Get_MaxPhases()
{
	return configurationStruct.maxPhases;
}

uint8_t storage_Get_DefaultOfflinePhase()
{
	return configurationStruct.defaultOfflinePhase;
}

float storage_Get_DefaultOfflineCurrent()
{
	return configurationStruct.defaultOfflineCurrent;
}

uint8_t storage_Get_IsEnabled()
{
	return configurationStruct.isEnabled;
}

char * storage_Get_InstallationId()
{
	//Sanity check
	if(strlen(configurationStruct.installationId) < 36)
		strcpy(configurationStruct.installationId, INSTALLATION_ID);

	return configurationStruct.installationId;
}

char * storage_Get_RoutingId()
{
	//Sanity check
	int len = strlen(configurationStruct.routingId);
	if(len == 0)
		strcpy(configurationStruct.routingId, ROUTING_ID);

	return configurationStruct.routingId;
}

char * storage_Get_ChargerName()
{
	return configurationStruct.chargerName;
}

uint32_t storage_Get_DiagnosticsMode()
{
	return configurationStruct.diagnosticsMode;
}

uint32_t storage_Get_TransmitInterval()
{
	//Sanity check. On old chargers the default is 120. Don't use this frequent defaults when updated with nvs read parameter.
	if (configurationStruct.transmitInterval == 120)
		configurationStruct.transmitInterval = DEFAULT_TRANSMIT_INTERVAL;
	else if (configurationStruct.transmitInterval == 3600)
		configurationStruct.transmitInterval = DEFAULT_TRANSMIT_INTERVAL;
	//If 0, return (disable logging)
	else if(configurationStruct.transmitInterval == 0)
		return configurationStruct.transmitInterval;
	else if (configurationStruct.transmitInterval < 10)
		configurationStruct.transmitInterval = 10;
	else if (configurationStruct.transmitInterval > 86400)
		configurationStruct.transmitInterval = 86400;

	return configurationStruct.transmitInterval;
}

float storage_Get_TransmitChangeLevel()
{
	return configurationStruct.transmitChangeLevel;
}

//Ocpp settings
const char * storage_Get_url_ocpp()
{
	return configurationStruct.url_ocpp;
}

enum session_controller storage_Get_session_controller()
{
	return configurationStruct.session_controller;
}

bool storage_Get_ocpp_authorize_remote_tx_requests()
{
	return configurationStruct.ocpp_authorize_remote_tx_requests;
}

uint32_t storage_Get_ocpp_clock_aligned_data_interval()
{
	return configurationStruct.ocpp_clock_aligned_data_interval;
}

uint32_t storage_Get_ocpp_connection_timeout()
{
	return configurationStruct.ocpp_connection_timeout;
}

uint8_t storage_Get_ocpp_connector_phase_rotation_max_length()
{
	return configurationStruct.ocpp_connector_phase_rotation_max_length;
}

uint8_t storage_Get_ocpp_get_configuration_max_keys()
{
	return configurationStruct.ocpp_get_configuration_max_keys;
}

uint32_t storage_Get_ocpp_heartbeat_interval()
{
	return configurationStruct.ocpp_heartbeat_interval;
}

bool storage_Get_ocpp_local_authorize_offline()
{
	return configurationStruct.ocpp_local_authorize_offline;
}

bool storage_Get_ocpp_local_pre_authorize()
{
	return configurationStruct.ocpp_local_pre_authorize;
}

const char * storage_Get_ocpp_meter_values_aligned_data()
{
	return configurationStruct.ocpp_meter_values_aligned_data;
}

uint8_t storage_Get_ocpp_meter_values_aligned_data_max_length()
{
	return configurationStruct.ocpp_meter_values_aligned_data_max_length;
}

const char * storage_Get_ocpp_meter_values_sampled_data()
{
	return configurationStruct.ocpp_meter_values_sampled_data;
}

uint8_t storage_Get_ocpp_meter_values_sampled_data_max_length()
{
	return configurationStruct.ocpp_meter_values_sampled_data_max_length;
}

uint32_t storage_Get_ocpp_meter_value_sample_interval()
{
	return configurationStruct.ocpp_meter_value_sample_interval;
}

uint8_t storage_Get_ocpp_number_of_connectors()
{
	return configurationStruct.ocpp_number_of_connectors;
}

uint8_t storage_Get_ocpp_reset_retries()
{
	return configurationStruct.ocpp_reset_retries;
}

bool storage_Get_ocpp_stop_transaction_on_ev_side_disconnect()
{
	return configurationStruct.ocpp_stop_transaction_on_ev_side_disconnect;
}

bool storage_Get_ocpp_stop_transaction_on_invalid_id()
{
	return configurationStruct.ocpp_stop_transaction_on_invalid_id;
}

const char * storage_Get_ocpp_stop_txn_aligned_data()
{
	return configurationStruct.ocpp_stop_txn_aligned_data;
}

uint8_t storage_Get_ocpp_stop_txn_aligned_data_max_length()
{
	return configurationStruct.ocpp_stop_txn_aligned_data_max_length;
}

const char * storage_Get_ocpp_stop_txn_sampled_data()
{
	return configurationStruct.ocpp_stop_txn_sampled_data;
}

uint8_t storage_Get_ocpp_stop_txn_sampled_data_max_length()
{
	return configurationStruct.ocpp_stop_txn_sampled_data_max_length;
}

const char * storage_Get_ocpp_supported_feature_profiles()
{
	return configurationStruct.ocpp_supported_feature_profiles;
}

uint8_t storage_Get_ocpp_supported_feature_profiles_max_length()
{
	return configurationStruct.ocpp_supported_feature_profiles_max_length;
}

uint8_t storage_Get_ocpp_transaction_message_attempts()
{
	return configurationStruct.ocpp_transaction_message_attempts;
}

uint16_t storage_Get_ocpp_transaction_message_retry_interval()
{
	return configurationStruct.ocpp_transaction_message_retry_interval;
}

bool storage_Get_ocpp_unlock_connector_on_ev_side_disconnect()
{
	return configurationStruct.ocpp_unlock_connector_on_ev_side_disconnect;
}

bool storage_Get_ocpp_local_auth_list_enabled()
{
	return configurationStruct.ocpp_local_auth_list_enabled;
}

uint16_t storage_Get_ocpp_local_auth_list_max_length()
{
	return configurationStruct.ocpp_local_auth_max_length;
}

uint8_t storage_Get_ocpp_send_local_list_max_length()
{
	return configurationStruct.ocpp_send_local_list_max_length;
}


//Local settings

uint8_t storage_Get_CommunicationMode()
{
	return configurationStruct.communicationMode;
}

float storage_Get_HmiBrightness()
{
	return configurationStruct.hmiBrightness;
}

uint8_t storage_Get_PermanentLock()
{
	return configurationStruct.permanentLock;
}

uint8_t storage_Get_Standalone()
{
	return configurationStruct.standalone;
}

uint8_t storage_Get_StandalonePhase()
{
	return configurationStruct.standalonePhase;
}

float storage_Get_StandaloneCurrent()
{
	return configurationStruct.standaloneCurrent;
}

float storage_Get_MaxInstallationCurrentConfig()
{
	return configurationStruct.maxInstallationCurrentConfig;
}

uint8_t storage_Get_PhaseRotation()
{
	//For new chargers with no PhaseRotation = 0, set default if configured with switch
	if(configurationStruct.phaseRotation == 0)
	{
		if(MCU_GetGridType() == NETWORK_1P4W)
			return 1;
		if(MCU_GetGridType() == NETWORK_3P4W)
			return 4;
		if(MCU_GetGridType() == NETWORK_1P3W)
			return 10;
		if(MCU_GetGridType() == NETWORK_3P3W)
			return 13;
	}
	//Previous incorrect default value (1) is used on previous chargers
	else if(configurationStruct.phaseRotation == 1)
	{
		//For the various grid types not matching phase rotation 1, set default for grid type
		if(MCU_GetGridType() == NETWORK_3P4W)
			return 4;
		if(MCU_GetGridType() == NETWORK_1P3W)
			return 10;
		if(MCU_GetGridType() == NETWORK_3P3W)
			return 13;
	}

	//For chargers configured with App -> use configuration
	return configurationStruct.phaseRotation;
}

//This is read from MCU instead and is currently not used
/*uint8_t storage_Get_NetworkType()
{
	//If the override value has been set to a valid value, the override measurement value
	if(configurationStruct.networkTypeOverride != 0)
	{
		SetEspNotification(eNOTIFICATION_NETWORK_TYPE_OVERRIDE);
		return configurationStruct.networkTypeOverride;
	}
	else
	{
		return configurationStruct.networkType;
	}
}*/

/*uint8_t storage_Get_NetworkTypeOverride()
{
	return configurationStruct.networkTypeOverride;
}*/

uint32_t storage_Get_PulseInterval()
{
	//Sanity check. 0 is default on new chargers. Set it to 60 as default for now.
	if (configurationStruct.pulseInterval == 0)
		configurationStruct.pulseInterval = 60;
	else if (configurationStruct.pulseInterval < 10)
		configurationStruct.pulseInterval = 10;
	else if (configurationStruct.pulseInterval > 3600)
		configurationStruct.pulseInterval = 3600;

	return configurationStruct.pulseInterval;
}


char * storage_Get_DiagnosticsLog()
{
	return configurationStruct.diagnosticsLog;
}

int storage_Get_DiagnosticsLogLength()
{
	return strlen(configurationStruct.diagnosticsLog);
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
	memcpy(outputValue, &intToFloat, 4);

	return err;
}


esp_err_t nvs_set_zdouble(nvs_handle_t handle, const char* key, double inputValue)
{
	uint64_t doubleToInt;
	memcpy(&doubleToInt, &inputValue, 8);
	err = nvs_set_u64(handle, key, doubleToInt);
	return err;
}

esp_err_t nvs_get_zdouble(nvs_handle_t handle, const char* key, double * outputValue)
{
	uint64_t intToDouble;
	err = nvs_get_u64(handle, key, &intToDouble);
	memcpy(outputValue, &intToDouble, 8);

	return err;
}


esp_err_t storage_SaveConfiguration()
{
	volatile esp_err_t err;

	err = nvs_open(CONFIG_FILE, NVS_READWRITE, &configuration_handle);

	configurationStruct.saveCounter++;
	err += nvs_set_u32(configuration_handle, "SaveCounter", configurationStruct.saveCounter);

	//Cloud settings
	err += nvs_set_u8(configuration_handle, "AuthRequired", configurationStruct.authenticationRequired);
	err += nvs_set_zfloat(configuration_handle, "CurrInMax", configurationStruct.currentInMaximum);
	err += nvs_set_zfloat(configuration_handle, "CurrInMin", configurationStruct.currentInMinimum);
	err += nvs_set_u8(configuration_handle, "MaxPhases", configurationStruct.maxPhases);
	err += nvs_set_u8(configuration_handle, "DefOfflinePh", configurationStruct.defaultOfflinePhase);
	err += nvs_set_zfloat(configuration_handle, "DefOfflineCurr", configurationStruct.defaultOfflineCurrent);
	err += nvs_set_u8(configuration_handle, "IsEnabled", configurationStruct.isEnabled);
	err += nvs_set_str(configuration_handle, "InstId", configurationStruct.installationId);
	err += nvs_set_str(configuration_handle, "RoutId", configurationStruct.routingId);
	err += nvs_set_str(configuration_handle, "ChargerName", configurationStruct.chargerName);
	err += nvs_set_u32(configuration_handle, "DiagMode", configurationStruct.diagnosticsMode);
	err += nvs_set_u32(configuration_handle, "TxInterval", configurationStruct.transmitInterval);
	err += nvs_set_zfloat(configuration_handle, "TxChangeLevel", configurationStruct.transmitChangeLevel);

	//OCPP settings
	err += nvs_set_str(configuration_handle, "urlOcpp", configurationStruct.url_ocpp);
	err += nvs_set_u8(configuration_handle, "sessionCtrl", configurationStruct.session_controller);
	//err += nvs_set_u8(configuration_handle, "oAllowTxUnknown", configurationStruct.ocpp_allow_offline_tx_for_unknown_id);
	//err += nvs_set_u8(configuration_handle, "oAuthCachEnable", configurationStruct.ocpp_authorization_cache_enabled);
	err += nvs_set_u8(configuration_handle, "oAuthRemoteTx", configurationStruct.ocpp_authorize_remote_tx_requests);
	//err += nvs_set_u8(configuration_handle, "oBlinkRequest", configurationStruct.ocpp_blink_repeats);
	err += nvs_set_u32(configuration_handle, "oClockAligned", configurationStruct.ocpp_clock_aligned_data_interval);
	err += nvs_set_u32(configuration_handle, "oConTimeout", configurationStruct.ocpp_connection_timeout);
	//err += nvs_set_str(configuration_handle, "oConPhaseRotate", configurationStruct.ocpp_connector_phase_rotation);
	err += nvs_set_u8(configuration_handle, "oConPhaseRot_ml", configurationStruct.ocpp_connector_phase_rotation_max_length);
	err += nvs_set_u8(configuration_handle, "oConfig_mk", configurationStruct.ocpp_get_configuration_max_keys);
	err += nvs_set_u32(configuration_handle, "oHeartbeatInter", configurationStruct.ocpp_heartbeat_interval);
	//err += nvs_set_u8(configuration_handle, "oLightIntensity", configurationStruct.ocpp_light_intensity);
	err += nvs_set_u8(configuration_handle, "oAuthOffline", configurationStruct.ocpp_local_authorize_offline);
	err += nvs_set_u8(configuration_handle, "oPreAuth", configurationStruct.ocpp_local_pre_authorize);
	//err += nvs_set_u32(configuration_handle, "oEnergyOnInv_m", configurationStruct.ocpp_max_energy_on_invalid_id);
	err += nvs_set_str(configuration_handle, "oMtrValAlign", configurationStruct.ocpp_meter_values_aligned_data);
	err += nvs_set_u8(configuration_handle, "oMtrValAlign_ml", configurationStruct.ocpp_meter_values_aligned_data_max_length);
	err += nvs_set_str(configuration_handle, "oMtrValSampl", configurationStruct.ocpp_meter_values_sampled_data);
	err += nvs_set_u8(configuration_handle, "oMtrValSampl_ml", configurationStruct.ocpp_meter_values_sampled_data_max_length);
	err += nvs_set_u32(configuration_handle, "oMtrValSamplInt", configurationStruct.ocpp_meter_value_sample_interval);
	//err += nvs_set_u32(configuration_handle, "oStatusDur_min", configurationStruct.ocpp_minimum_status_duration);
	err += nvs_set_u8(configuration_handle, "oNumbOfConnect", configurationStruct.ocpp_number_of_connectors);
	err += nvs_set_u8(configuration_handle, "oResetRetries", configurationStruct.ocpp_reset_retries);
	err += nvs_set_u8(configuration_handle, "oStopTxnEvDisc", configurationStruct.ocpp_stop_transaction_on_ev_side_disconnect);
	err += nvs_set_u8(configuration_handle, "oStopTxnOnInval", configurationStruct.ocpp_stop_transaction_on_invalid_id);
	err += nvs_set_str(configuration_handle, "oStopTxnAlign", configurationStruct.ocpp_stop_txn_aligned_data);
	err += nvs_set_u8(configuration_handle, "oStopTxnAlig_ml", configurationStruct.ocpp_stop_txn_aligned_data_max_length);
	err += nvs_set_str(configuration_handle, "oStopTxnSamp", configurationStruct.ocpp_stop_txn_sampled_data);
	err += nvs_set_u8(configuration_handle, "oStopTxnSamp_ml", configurationStruct.ocpp_stop_txn_sampled_data_max_length);
	err += nvs_set_str(configuration_handle, "oSuppFeature", configurationStruct.ocpp_supported_feature_profiles);
	err += nvs_set_u8(configuration_handle, "oSuppFeature_ml", configurationStruct.ocpp_supported_feature_profiles_max_length);
	err += nvs_set_u8(configuration_handle, "oTxnAttempts", configurationStruct.ocpp_transaction_message_attempts);
	err += nvs_set_u16(configuration_handle, "oTxnRetryInter", configurationStruct.ocpp_transaction_message_retry_interval);
	err += nvs_set_u8(configuration_handle, "oUlockConEvDisc", configurationStruct.ocpp_unlock_connector_on_ev_side_disconnect);
	//err += nvs_set_(configuration_handle, "oWebSockPingInt", configurationStruct.web_socket_ping_interval);
	err += nvs_set_u8(configuration_handle, "oAuthLEnabled", configurationStruct.ocpp_local_auth_list_enabled);
	err += nvs_set_u16(configuration_handle, "oAuthL_ml", configurationStruct.ocpp_local_auth_max_length);
	err += nvs_set_u8(configuration_handle, "oSendAuthl_ml", configurationStruct.ocpp_send_local_list_max_length);

	//Local settings
	err += nvs_set_u8(configuration_handle, "ComMode", configurationStruct.communicationMode);
	err += nvs_set_zfloat(configuration_handle, "HmiBrightness", configurationStruct.hmiBrightness);
	err += nvs_set_u8(configuration_handle, "PermanentLock", configurationStruct.permanentLock);
	err += nvs_set_u8(configuration_handle, "Stdalone", configurationStruct.standalone);
	err += nvs_set_u8(configuration_handle, "StdalonePhase", configurationStruct.standalonePhase);
	err += nvs_set_zfloat(configuration_handle, "StdaloneCurr", configurationStruct.standaloneCurrent);
	err += nvs_set_zfloat(configuration_handle, "maxInstCurrConf", configurationStruct.maxInstallationCurrentConfig);
	err += nvs_set_u8(configuration_handle, "PhaseRotation", configurationStruct.phaseRotation);
	err += nvs_set_u8(configuration_handle, "NetworkType", configurationStruct.networkType);
	err += nvs_set_u8(configuration_handle, "NetworkTypeOv", configurationStruct.networkTypeOverride);
	err += nvs_set_u32(configuration_handle, "PulseInterval", configurationStruct.pulseInterval);

	err += nvs_set_str(configuration_handle, "DiagnosticsLog", configurationStruct.diagnosticsLog);

	err += nvs_commit(configuration_handle);
	nvs_close(configuration_handle);

	//memset(&configurationStruct, 0, sizeof(configurationStruct));

	return err;
}


esp_err_t storage_ReadConfiguration()
{
	esp_err_t err;
	err = nvs_open(CONFIG_FILE, NVS_READONLY, &configuration_handle);

	err += nvs_get_u32(configuration_handle, "SaveCounter", &configurationStruct.saveCounter);

	//Cloud settings
	err += nvs_get_u8(configuration_handle, "AuthRequired", &configurationStruct.authenticationRequired);
	err += nvs_get_zfloat(configuration_handle, "CurrInMax", &configurationStruct.currentInMaximum);
	err += nvs_get_zfloat(configuration_handle, "CurrInMin", &configurationStruct.currentInMinimum);
	err += nvs_get_u8(configuration_handle, "MaxPhases", &configurationStruct.maxPhases);
	err += nvs_get_u8(configuration_handle, "DefOfflinePh", &configurationStruct.defaultOfflinePhase);
	err += nvs_get_zfloat(configuration_handle, "DefOfflineCurr", &configurationStruct.defaultOfflineCurrent);
	err += nvs_get_u8(configuration_handle, "IsEnabled", &configurationStruct.isEnabled);
	size_t readSize;
	err += nvs_get_str(configuration_handle, "InstId", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "InstId", configurationStruct.installationId, &readSize);
	err += nvs_get_str(configuration_handle, "RoutId", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "RoutId", configurationStruct.routingId, &readSize);
	err += nvs_get_str(configuration_handle, "ChargerName", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "ChargerName", configurationStruct.chargerName, &readSize);
	err += nvs_get_u32(configuration_handle, "DiagMode", &configurationStruct.diagnosticsMode);
	err += nvs_get_u32(configuration_handle, "TxInterval", &configurationStruct.transmitInterval);
	err += nvs_get_zfloat(configuration_handle, "TxChangeLevel", &configurationStruct.transmitChangeLevel);

	//OCPP settings
	readSize = URL_OCPP_MAX_LENGTH;
	err += nvs_get_str(configuration_handle, "urlOcpp", configurationStruct.url_ocpp, &readSize);
	err += nvs_get_u8(configuration_handle, "sessionCtrl", (uint8_t *)&configurationStruct.session_controller);
	//err += nvs_get_u8(configuration_handle, "oAllowTxUnknown", &configurationStruct.ocpp_allow_offline_tx_for_unknown_id);
	//err += nvs_get_u8(configuration_handle, "oAuthCachEnable", &configurationStruct.ocpp_authorization_cache_enabled);
	err += nvs_get_u8(configuration_handle, "oAuthRemoteTx", (uint8_t *)&configurationStruct.ocpp_authorize_remote_tx_requests);
	//err += nvs_get_u8(configuration_handle, "oBlinkRequest", &configurationStruct.ocpp_blink_repeats);
	err += nvs_get_u32(configuration_handle, "oClockAligned", &configurationStruct.ocpp_clock_aligned_data_interval);
	err += nvs_get_u32(configuration_handle, "oConTimeout", &configurationStruct.ocpp_connection_timeout);
	//err += nvs_get_str(configuration_handle, "oConPhaseRotate", &configurationStruct.ocpp_connector_phase_rotation);
	err += nvs_get_u8(configuration_handle, "oConPhaseRot_ml", &configurationStruct.ocpp_connector_phase_rotation_max_length);
	err += nvs_get_u8(configuration_handle, "oConfig_mk", &configurationStruct.ocpp_get_configuration_max_keys);
	err += nvs_get_u32(configuration_handle, "oHeartbeatInter", &configurationStruct.ocpp_heartbeat_interval);
	//err += nvs_get_u8(configuration_handle, "oLightIntensity", &configurationStruct.ocpp_light_intensity);
	err += nvs_get_u8(configuration_handle, "oAuthOffline", (uint8_t *)&configurationStruct.ocpp_local_authorize_offline);
	err += nvs_get_u8(configuration_handle, "oPreAuth", (uint8_t *)&configurationStruct.ocpp_local_pre_authorize);
	//err += nvs_get_u32(configuration_handle, "oEnergyOnInv_m", &configurationStruct.ocpp_max_energy_on_invalid_id);
	readSize = DEFAULT_CSL_SIZE;
	err += nvs_get_str(configuration_handle, "oMtrValAlign", configurationStruct.ocpp_meter_values_aligned_data, &readSize);
	err += nvs_get_u8(configuration_handle, "oMtrValAlign_ml", &configurationStruct.ocpp_meter_values_aligned_data_max_length);
	readSize = DEFAULT_CSL_SIZE;
	err += nvs_get_str(configuration_handle, "oMtrValSampl", configurationStruct.ocpp_meter_values_sampled_data, &readSize);
	err += nvs_get_u8(configuration_handle, "oMtrValSampl_ml", &configurationStruct.ocpp_meter_values_sampled_data_max_length);
	err += nvs_get_u32(configuration_handle, "oMtrValSamplInt", &configurationStruct.ocpp_meter_value_sample_interval);
	//err += nvs_get_u32(configuration_handle, "oStatusDur_min", &configurationStruct.ocpp_minimum_status_duration);
	err += nvs_get_u8(configuration_handle, "oNumbOfConnect", &configurationStruct.ocpp_number_of_connectors);
	err += nvs_get_u8(configuration_handle, "oResetRetries", &configurationStruct.ocpp_reset_retries);
	err += nvs_get_u8(configuration_handle, "oStopTxnEvDisc", (uint8_t *)&configurationStruct.ocpp_stop_transaction_on_ev_side_disconnect);
	err += nvs_get_u8(configuration_handle, "oStopTxnOnInval", (uint8_t *)&configurationStruct.ocpp_stop_transaction_on_invalid_id);
	readSize = DEFAULT_CSL_SIZE;
	err += nvs_get_str(configuration_handle, "oStopTxnAlign", configurationStruct.ocpp_stop_txn_aligned_data, &readSize);
	err += nvs_get_u8(configuration_handle, "oStopTxnAlig_ml", &configurationStruct.ocpp_stop_txn_aligned_data_max_length);
	readSize = DEFAULT_CSL_SIZE;
	err += nvs_get_str(configuration_handle, "oStopTxnSamp", configurationStruct.ocpp_stop_txn_sampled_data, &readSize);
	err += nvs_get_u8(configuration_handle, "oStopTxnSamp_ml", &configurationStruct.ocpp_stop_txn_sampled_data_max_length);
	readSize = 128;
	err += nvs_get_str(configuration_handle, "oSuppFeature", configurationStruct.ocpp_supported_feature_profiles, &readSize);
	err += nvs_get_u8(configuration_handle, "oSuppFeature_ml", &configurationStruct.ocpp_supported_feature_profiles_max_length);
	err += nvs_get_u8(configuration_handle, "oTxnAttempts", &configurationStruct.ocpp_transaction_message_attempts);
	err += nvs_get_u16(configuration_handle, "oTxnRetryInter", &configurationStruct.ocpp_transaction_message_retry_interval);
	err += nvs_get_u8(configuration_handle, "oUlockConEvDisc", (uint8_t *)&configurationStruct.ocpp_unlock_connector_on_ev_side_disconnect);
	//err += nvs_get_(configuration_handle, "oWebSockPingInt", &configurationStruct.web_socket_ping_interval);
	err += nvs_get_u8(configuration_handle, "oAuthLEnabled", (uint8_t *)&configurationStruct.ocpp_local_auth_list_enabled);
	err += nvs_get_u16(configuration_handle, "oAuthL_ml", &configurationStruct.ocpp_local_auth_max_length);
	err += nvs_get_u8(configuration_handle, "oSendAuthl_ml", &configurationStruct.ocpp_send_local_list_max_length);

	//Local settings
	err += nvs_get_u8(configuration_handle, "ComMode", &configurationStruct.communicationMode);
	err += nvs_get_zfloat(configuration_handle, "HmiBrightness", &configurationStruct.hmiBrightness);
	err += nvs_get_u8(configuration_handle, "PermanentLock", &configurationStruct.permanentLock);
	err += nvs_get_u8(configuration_handle, "Stdalone", &configurationStruct.standalone);
	err += nvs_get_u8(configuration_handle, "StdalonePhase", &configurationStruct.standalonePhase);
	err += nvs_get_zfloat(configuration_handle, "StdaloneCurr", &configurationStruct.standaloneCurrent);
	err += nvs_get_zfloat(configuration_handle, "maxInstCurrConf", &configurationStruct.maxInstallationCurrentConfig);
	err += nvs_get_u8(configuration_handle, "PhaseRotation", &configurationStruct.phaseRotation);
	err += nvs_get_u8(configuration_handle, "NetworkType", &configurationStruct.networkType);

	//Do not return error;
	nvs_get_u8(configuration_handle, "NetworkTypeOv", &configurationStruct.networkTypeOverride);
	nvs_get_u32(configuration_handle, "PulseInterval", &configurationStruct.pulseInterval);

	nvs_get_str(configuration_handle, "DiagnosticsLog", NULL, &readSize);
	nvs_get_str(configuration_handle, "DiagnosticsLog", configurationStruct.diagnosticsLog, &readSize);

	//ESP_LOGE(TAG, "TransmitInterval: 			%i", configurationStruct.transmitInterval);
	//ESP_LOGE(TAG, "PulseInterval: 				%i", configurationStruct.pulseInterval);


	//When adding more parameters, don't accumulate their error, since returning an error will cause all parameters to be reinitialized

	nvs_close(configuration_handle);

	//Do not make changes that return error;
	return err;
}

void storage_PrintConfiguration()
{

	//ESP_LOGW(TAG, "*********************************");
	ESP_LOGI(TAG, "AuthRequired: 				%i", configurationStruct.authenticationRequired);

	char comMode[5] = {0};
	if(configurationStruct.communicationMode == eCONNECTION_NONE)
		strcpy(comMode, "None");
	else if(configurationStruct.communicationMode == eCONNECTION_WIFI)
		strcpy(comMode, "Wifi");
	else if(configurationStruct.communicationMode == eCONNECTION_LTE)
		strcpy(comMode, "4G");
	ESP_LOGI(TAG, "CommunicationMode: 	\t		%s", comMode);


	ESP_LOGI(TAG, "MaxInstallationCurrenConfig:	\t\t%f", configurationStruct.maxInstallationCurrentConfig);
	ESP_LOGI(TAG, "Standalone: 					%i", configurationStruct.standalone);
	ESP_LOGI(TAG, "Standalone current: 	\t		%2.1f", configurationStruct.standaloneCurrent);
	ESP_LOGI(TAG, "");
	ESP_LOGI(TAG, "Maximum current: 	\t		%2.1f", configurationStruct.currentInMaximum);
	ESP_LOGI(TAG, "Minimum current: 	\t		%2.1f", configurationStruct.currentInMinimum);
	ESP_LOGI(TAG, "");
	ESP_LOGI(TAG, "RoutingId: 					%s", configurationStruct.routingId);
	ESP_LOGI(TAG, "InstallationId: 				%s", configurationStruct.installationId);

	ESP_LOGI(TAG, "TransmitInterval: 			\t%i", configurationStruct.transmitInterval);
	ESP_LOGI(TAG, "PulseInterval: 				%i", configurationStruct.pulseInterval);
	ESP_LOGI(TAG, "DiagnosticsLog: 				%s", configurationStruct.diagnosticsLog);


	//ESP_LOGW(TAG, "*********************************");
}

/* Obsoleted - Replaced with OfflineSessions

esp_err_t storage_SaveSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode)
{
	volatile esp_err_t err;

	err = nvs_open(CS_RESET_FILE, NVS_READWRITE, &session_reset_handle);

	err += nvs_set_str(session_reset_handle, "csId", csId);
	err += nvs_set_str(session_reset_handle, "csStartTime", csStartTime);
	err += nvs_set_u32(session_reset_handle, "csUnixTime", csUnixTime);
	err += nvs_set_zfloat(session_reset_handle, "csEnergy", csEnergy);
	err += nvs_set_str(session_reset_handle, "csAuthCode", csAuthCode);

	err += nvs_commit(session_reset_handle);
	nvs_close(session_reset_handle);

	return err;
}


esp_err_t storage_ReadSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode)
{
	size_t readSize = 0;

	esp_err_t err;
	err = nvs_open(CS_RESET_FILE, NVS_READONLY, &session_reset_handle);

	err += nvs_get_str(session_reset_handle, "csId", NULL, &readSize);
	//Only continue to read if there is a session start stored
	if((readSize > 0) && (err == ESP_OK))
	{
		err += nvs_get_str(session_reset_handle, "csId", csId, &readSize);

		err += nvs_get_str(session_reset_handle, "csStartTime", NULL, &readSize);
		err += nvs_get_str(session_reset_handle, "csStartTime", csStartTime, &readSize);

		err += nvs_get_u32(session_reset_handle, "csUnixTime", &csUnixTime);
		err += nvs_get_zfloat(session_reset_handle, "csEnergy", &csEnergy);

		err += nvs_get_str(session_reset_handle, "csAuthCode", NULL, &readSize);
		err += nvs_get_str(session_reset_handle, "csAuthCode", csAuthCode, &readSize);
	}

	nvs_close(session_reset_handle);

	return err;
}



size_t storage_CheckSessionResetFile()
{
	size_t readSize = 0;

	err = nvs_open(CS_RESET_FILE, NVS_READONLY, &session_reset_handle);
	err += nvs_get_str(session_reset_handle, "csId", NULL, &readSize);
	nvs_close(session_reset_handle);

	return readSize;
}


esp_err_t storage_clearSessionResetInfo()
{
	err = nvs_open(CS_RESET_FILE, NVS_READWRITE, &session_reset_handle);
	err += nvs_erase_all(session_reset_handle);
	err += nvs_commit(session_reset_handle);
	nvs_close(session_reset_handle);

	return err;
}*/




/*
 * If Authorization is set but no valid RFID-tag file is present this function should be called
 * to set the wildcard tag to allow all charging when offline. This will be held until Cloud performs a periodic
 * Tag-sync with the correct list.
 */
void storageSetWildCardTag()
{
	struct RFIDTokens rfidTokens[2];
	memset(rfidTokens,0,sizeof(rfidTokens));

	char wildTag[] = "*";
	rfidTokens[0].Tag = wildTag;
	rfidTokens[0].Action = 0; //Add tag

	storage_updateRFIDTagsToFile(rfidTokens, 1);
}

void storage_Verify_AuthenticationSetting()
{
	esp_err_t err = nvs_open("RFIDTags", NVS_READWRITE, &rfid_tag_handle);

	//Check if tag-file don't exist  - clear authentication
	if((configurationStruct.authenticationRequired == 1) && (err != ESP_OK))
	{
		storageSetWildCardTag();
		ESP_LOGW(TAG, "Auth set but no valid tag-file -> saving wildcard flag");
	}
	//If tag-file exist but has no valid tags - clear authentication
	else if((configurationStruct.authenticationRequired == 1) && (err == ESP_OK))
	{
		uint32_t nrOfTagsOnFile = 0;
		err = nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);
		//Key don't exist or value not set
		if((err != ESP_OK) || (nrOfTagsOnFile == 0))
		{
			storageSetWildCardTag();
			ESP_LOGW(TAG, "Auth set but 0 tags on file -> saving wildcard flag");
		}
	}

	nvs_close(rfid_tag_handle);
}

static uint32_t nrOfTagsCounter = 0;

esp_err_t storage_updateRFIDTagsToFile(volatile struct RFIDTokens rfidTokens[], uint32_t nrOfTokens)
{
	//Add(action = 0) to empty list or add to existing list up to MAX_NR_OF_RFID_TAGS.
	//Remove(action = 1) from list

	//Read existing nr of tags
	//If fewer than MAX_NR_OF_RFID_TAGS, then add.

	ESP_LOGI(TAG, "Cnt: %d, Token: %s, Action: %d", nrOfTokens, rfidTokens[0].Tag, rfidTokens[0].Action);

	err = nvs_open("RFIDTags", NVS_READWRITE, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	if(nrOfTagsOnFile > 20)
	{
		ESP_LOGE(TAG, "NrOfTagsOnfile %d", nrOfTagsOnFile);
		return -1;
	}


	int removed = 0;
	int moved = 0;
	int actuallySaved = 0;

	//Only try to remove tags if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//First check for tags to remove. Doing this first is most space optimal.
		for (int tagNr = 0; tagNr < nrOfTokens; tagNr++)
		{
			//Check if this tag should be removed
			if(rfidTokens[tagNr].Action == 1)
			{
				for (int tagNrOnFile = 0; tagNrOnFile < nrOfTagsOnFile; tagNrOnFile++)
				{
					if(nrOfTagsOnFile > MAX_NR_OF_RFID_TAGS)
					{
						err = nvs_commit(rfid_tag_handle);
						nvs_close(rfid_tag_handle);
						return ESP_ERR_INVALID_STATE;
					}

					size_t readSize;
					char keyErasePosName[15] = {0};
					sprintf(keyErasePosName, "TagNr%d", tagNrOnFile);

					//err += nvs_set_str(rfid_tag_handle, keyName, rfidTokens[tagNr].Tag);
					err += nvs_get_str(rfid_tag_handle, keyErasePosName, NULL, &readSize);

					//Don't allow excessive lengths
					if((readSize > (DEFAULT_STR_SIZE+4)) || (readSize == 0))
						continue;

					char tagId[DEFAULT_STR_SIZE+4] = {0};
					err += nvs_get_str(rfid_tag_handle, keyErasePosName, tagId, &readSize);

					//If match - remove
					if(strcmp(tagId, rfidTokens[tagNr].Tag) == 0)
					{
						err += nvs_erase_key(rfid_tag_handle, keyErasePosName);
						removed++;
						if(nrOfTagsOnFile >= 1)
							nrOfTagsOnFile--;
						ESP_LOGI(TAG, "Removed %s: %s, TagsOnFile: %d", keyErasePosName, rfidTokens[tagNr].Tag, nrOfTagsOnFile);

						//If there are more tags on file, move last item up to removed item.
						if(tagNrOnFile < nrOfTagsOnFile)
						{
							//Read the key to move
							char keyToMoveName[15] = {0};
							sprintf(keyToMoveName, "TagNr%d", nrOfTagsOnFile);
							err += nvs_get_str(rfid_tag_handle, keyToMoveName, NULL, &readSize);
							err += nvs_get_str(rfid_tag_handle, keyToMoveName, tagId, &readSize);

							//Write the key to move
							err += nvs_set_str(rfid_tag_handle, keyErasePosName, tagId);

							//Erase the moved keys previous position
							err += nvs_erase_key(rfid_tag_handle, keyToMoveName);

							ESP_LOGI(TAG, "Moved %s -> %s", keyToMoveName, keyErasePosName);
							moved++;
						}
					}
				}
			}
		}
	}

	int nrOfTagsOnFileInit = nrOfTagsOnFile;

	//Add tags to list if not already there
	for (int tagNr = 0; tagNr < nrOfTokens; tagNr++)
	{
		ESP_LOGI(TAG, "Action: %d",rfidTokens[tagNr].Action);

		//Check if this tag should be added
		if(rfidTokens[tagNr].Action == 0)
		{
			bool tagDuplicate = false;

			//Check if tag is already in saved list
			for (int tagOnFile = 0; tagOnFile < nrOfTagsOnFileInit; tagOnFile++)
			{
				size_t readSize;
				char keyName[15] = {0};
				sprintf(keyName, "TagNr%d", tagOnFile);

				err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

				//Don't allow excessive lengths
				if((readSize > (DEFAULT_STR_SIZE+4)) || (readSize == 0))
					continue;

				char tagId[DEFAULT_STR_SIZE+4] = {0};
				err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

				//If match - flag as duplicate
				if(strcmp(tagId, rfidTokens[tagNr].Tag) == 0)
				{
					ESP_LOGE(TAG, "%s duplicate!",keyName);
					tagDuplicate = true;
					break;
				}
			}

			//If we have found a duplicate, don't write, just continue with next tag
			if(tagDuplicate == true)
				continue;

			if(nrOfTagsOnFile < MAX_NR_OF_RFID_TAGS)
			{
				char keyName[15] = {0};
				sprintf(keyName, "TagNr%d", nrOfTagsOnFile);//Key name continuing from flash numbering
				err += nvs_set_str(rfid_tag_handle, keyName, rfidTokens[tagNr].Tag);
				actuallySaved++;
				nrOfTagsOnFile++;
			}
			else
			{
				//Error more than MAX_NR_OF_RFID_TAGS //TODO: Communicate to Cloud and app
				ESP_LOGE(TAG, "More tokens than allowed: %d", nrOfTagsOnFile);
			}


		}
	}

	nrOfTagsCounter = nrOfTagsOnFile;

	//Update nr of NrOfTagsSaved
	err += nvs_set_u32(rfid_tag_handle, "NrOfTagsSaved", nrOfTagsOnFile);

	err = nvs_commit(rfid_tag_handle);
	nvs_close(rfid_tag_handle);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Removed: %d/%d, Saved: %d/%d, Moved: %d. On file: %d.", removed, nrOfTokens, actuallySaved, nrOfTokens, moved, nrOfTagsOnFile);
	else
		ESP_LOGI(TAG, "ERROR %d when saving %d new rfidTags",err, nrOfTokens);

	return err;
}



esp_err_t storage_lookupRFIDTagInList(char * tag, uint8_t *match)
{
	err = nvs_open("RFIDTags", NVS_READONLY, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	//Only try to lookup if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//Look for tag
		for (uint8_t tagNr = 0; tagNr < nrOfTagsOnFile; tagNr++)
		{
			//Make key to search
			size_t readSize;
			char keyName[17] = {0};
			sprintf(keyName, "TagNr%d", tagNr);

			//Get tagId string length
			err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

			//Don't allow excessive lengths
			if(readSize > 50)
				continue;

			//Read tagId string
			char tagId[readSize];
			err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

			//If match set return value
			if(strcmp(tagId, "*") == 0)
			{
				*match = 1;
				ESP_LOGW(TAG, "Found match %s == *, TagsOnFile: %d", keyName, nrOfTagsOnFile);
				break;
			}

			//If match set return value
			if(strcmp(tagId, tag) == 0)
			{
				*match = 1;
				ESP_LOGW(TAG, "Found match %s == %s, TagsOnFile: %d", keyName, tag, nrOfTagsOnFile);
				break;
			}

		}
		if(*match == 0)
		{
			ESP_LOGW(TAG, "No match with %d TagsOnFile",  nrOfTagsOnFile);
		}

	}
	else
	{
		ESP_LOGI(TAG, "Nothing to search, %d TagsOnFile",  nrOfTagsOnFile);
	}

	nvs_close(rfid_tag_handle);
	return err;
}

char * RFIDbuffer = NULL;

void storage_CreateRFIDbuffer()
{
	ESP_LOGI(TAG, "Creating RFIDbuffer");
	RFIDbuffer = calloc(1000,1);
}

char * storage_GetRFIDbuffer()
{
	ESP_LOGI(TAG, "Reading RFIDbuffer");
	return RFIDbuffer;
}

void storage_FreeRFIDbuffer()
{
	ESP_LOGI(TAG, "Freed RFIDbuffer");
	free(RFIDbuffer);
}


//Read from file
uint32_t storage_ReadNrOfTagsOnFile()
{
	err = nvs_open("RFIDTags", NVS_READONLY, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);
	nvs_close(rfid_tag_handle);

	nrOfTagsCounter = nrOfTagsOnFile;

	return nrOfTagsOnFile;
}

//Get from memory to avoid file access every second in case of change
uint32_t storage_GetNrOfTagsCounter()
{
	return nrOfTagsCounter;
}

esp_err_t storage_printRFIDTagsOnFile(bool writeToBuffer)
{
	err = nvs_open("RFIDTags", NVS_READONLY, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	//Only try to lookup if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//Look for tag
		for (uint8_t tagNr = 0; tagNr < nrOfTagsOnFile; tagNr++)
		{
			//Make key to search
			size_t readSize;
			char keyName[17] = {0};
			sprintf(keyName, "TagNr%d", tagNr);

			//Get tagId string length
			err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

			//Don't allow excessive lengths
			if(readSize > 50)
				continue;

			//Read tagId string
			char tagId[readSize];
			err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

			if(writeToBuffer && (RFIDbuffer != NULL))
				sprintf(RFIDbuffer+strlen(RFIDbuffer), "%d/%d: %s -> %s\r\n", tagNr+1, nrOfTagsOnFile, keyName, tagId);
			ESP_LOGI(TAG, "%d/%d: %s -> %s", tagNr+1, nrOfTagsOnFile, keyName, tagId);
		}
	}
	else
	{
		ESP_LOGI(TAG, "No tags on file: tagsOnFile: %d ", nrOfTagsOnFile);
	}

	nvs_close(rfid_tag_handle);
	return err;
}



esp_err_t storage_clearAllRFIDTagsOnFile()
{
	err = nvs_open("RFIDTags", NVS_READWRITE, &rfid_tag_handle);
	err += nvs_erase_all(rfid_tag_handle);
	err += nvs_commit(rfid_tag_handle);
	nvs_close(rfid_tag_handle);

	nrOfTagsCounter = 0;

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
	struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();
	if(devInfo.factory_stage != FactoryStageFinnished){
		ESP_LOGW(TAG, "Using factory SSID and PSK!!");
		// strcpy(SSID, "arntnett");
		// strcpy(PSK, "4703c87e817842c4ce6b167d43701b7685693846db");

		strcpy(SSID, "ZapWan");

		//Scramble to avoid readable string in bin-file
		int tmp = 13179704;
		tmp *= 4;
		sprintf(PSK,"%d", tmp);
		//strcpy(WifiPSK, "52718816");

		return 0;
	}

	size_t readSize;

	err = nvs_open("wifi", NVS_READONLY, &wifi_handle);

	err += nvs_get_str(wifi_handle, "WifiSSID", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiSSID", SSID, &readSize);

	err += nvs_get_str(wifi_handle, "WifiPSK", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiPSK", PSK, &readSize);

	ESP_LOGI(TAG, "Storage read Wifi: SSID: %s PSK len: %d",SSID, strlen(PSK));

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


void storage_GetStats(char * stat)
{
	nvs_stats_t nvs_stats;
	nvs_get_stats(NULL, &nvs_stats);
	sprintf(stat, "Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)", nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
	ESP_LOGI(TAG, "%s", stat);
}

double storage_update_accumulated_energy(float session_energy){
	double result = -1.0;

	nvs_handle_t handle;
	esp_err_t open_result = nvs_open("energy", NVS_READWRITE, &handle);
	if(open_result != ESP_OK ){
		ESP_LOGE(TAG, "failed to open NVS energy %d", open_result);
		result =  -2.0;
		goto err;
	}

	bool accumulator_initialised = false;
	float previous_session_energy;
	double previous_accumulated_energy;

	esp_err_t session_read_result =  nvs_get_zfloat(handle, "session", &previous_session_energy);
	esp_err_t accumulated_read_result = nvs_get_zdouble(handle, "accumulated", &previous_accumulated_energy);
	
	if(
		   (session_read_result == ESP_ERR_NVS_NOT_FOUND) 
		&& (accumulated_read_result == ESP_ERR_NVS_NOT_FOUND)
	){
		ESP_LOGW(TAG, "initing energy accumulation");
		previous_session_energy = 0.0;
		previous_accumulated_energy = 0.0;
		accumulator_initialised = true;
		
	}else if ((session_read_result != ESP_OK) || (accumulated_read_result != ESP_OK)){
		ESP_LOGE(TAG, "Very unexpected energy NVS state!!, %d and %d",session_read_result, accumulated_read_result );
		result = -10.0;
		// could we do cleanup here?
		goto err;
	}

	ESP_LOGW(TAG, "Energy accumulation inputs: ses %f, pses %f, pacc %f",
		session_energy, previous_session_energy, previous_accumulated_energy
	);

	if(session_energy<0.0){
		ESP_LOGW(TAG, "energy count not updated from dsPIC yet, using stale value: %f",
			previous_accumulated_energy
		);
		result = previous_accumulated_energy;
		goto err;
	}
	else if(session_energy > previous_session_energy){
		//if the energy count from the dspic has reset and passed previous_session_energy
		// we may loose some energy in this calculation,
		// tough normally this should be fine
		result = previous_accumulated_energy + (session_energy - previous_session_energy);
	}
	else if (session_energy < previous_session_energy){
		// dspic has started new session
		result = previous_accumulated_energy + session_energy;
		ESP_LOGW(TAG, "### Energy reset - new session ### %f < %f", session_energy, previous_session_energy);
	}
	else{
		if(accumulator_initialised == true){
			result = 0.0;
		}else{
			ESP_LOGW(TAG, "no change in energy");
			result = previous_accumulated_energy;
			ESP_LOGW(TAG, "updating total energy not needed %f -> %f (%f -> %f )",
				previous_accumulated_energy, result, previous_session_energy, session_energy
			);
			goto err;
		}
	}

	ESP_LOGW(TAG, "UPDATING total energy %f -> %f (%f -> %f )",
		previous_accumulated_energy, result, previous_session_energy, session_energy
	);

	esp_err_t session_write_result = nvs_set_zfloat(handle, "session", session_energy);
	esp_err_t accumulated_write_result = nvs_set_zdouble(handle, "accumulated", result);

	if((session_write_result!= ESP_OK) || (accumulated_write_result) != ESP_OK){
		ESP_LOGE(TAG, "Failed to write results, skiping commit (%d, %d)", 
		session_write_result, accumulated_write_result );
		goto err;
	}

	// documentation is unclear on the atomicity of the nvs operations
	esp_err_t commit_result = nvs_commit(handle);
	if(commit_result != ESP_OK){
		ESP_LOGE(TAG, "Failed to commit the result");
		// since everything worked up to this point, the return value should be valid
		// since we dont store anything, we should a new chance to store the correct data on the next calculation
		// but we increase the chance of loosing data, and have to persist it at some time
	}

	err:
	nvs_close(handle);
	return result;
}

int storage_clear_accumulated_energy(){
	nvs_handle handle;
	nvs_open("energy", NVS_READWRITE, &handle);
	nvs_erase_all(handle);
	nvs_commit(handle);
	nvs_close(handle);

	return 0;
}
