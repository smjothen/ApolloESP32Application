/*
 * DeviceInfo.h
 *
 *  Created on: 17. aug. 2020
 *      Author: vv
 */

#ifndef DEVICEINFO_H_
#define DEVICEINFO_H_

//#define DISABLE_LOGGING

enum FactoryStage {FactoryStageUnknown=0xff, FactoryStageUnknown2 = 0, FactoryStagComponentsTested=1, FactoryStageFinnished = 16};

struct DeviceInfo
{
	uint8_t EEPROMFormatVersion;
	uint8_t factory_stage;
	char serialNumber[10];
	char PSK[45];
	char Pin[5];
};

uint8_t GetEEPROMFormatVersion();
char * GetSoftwareVersion();
char * GetSoftwareVersionBLE();


#define ROUTING_ID "default"
#define INSTALLATION_ID "00000000-0000-0000-0000-000000000000"
#define INSTALLATION_ID_BASE64 "AAAAAAAAAAAAAAAAAAAAAA"

#define MAX_NR_OF_RFID_TAGS 20

struct RFIDTokens{
	char *Tag;//[37];
	int Action;
	char *ExpiryDate;//[37];
};


#define MAX_CERTIFICATE_SIZE 		50000
#define MAX_CERTIFICATE_BUNDLE_SIZE 51000

#define DEFAULT_STR_SIZE 37//Must be at least 37 for GUID! This value is also used in sscanf function!
#define PREFIX_GUID 41
#define DIAGNOSTICS_STRING_SIZE 100

#define URL_OCPP_MAX_LENGTH 128
#define DEFAULT_CSL_LENGTH 6//ocpp uses Comma Seperated Lists, optionally limited by length (nr of items)
#define DEFAULT_CSL_SIZE DEFAULT_CSL_LENGTH * 32 //list items like measurand vary between 3 char and 31 + phase

// Network IDs
#define NETWORK_1P3W 1
#define NETWORK_3P3W 2
#define NETWORK_1P4W 3
#define NETWORK_3P4W 4

//PulseRates
#define PULSE_INIT 120
#define PULSE_STANDALONE 900
#define PULSE_SYSTEM_NOT_CHARGING 600
#define PULSE_SYSTEM_CHARGING 180


//Numbers should match Pro
enum CommunicationMode
{
	eCONNECTION_NONE 		 = 0,
	eCONNECTION_WIFI		 = 1,
	eCONNECTION_LTE			 = 5,
	eCONNECTION_LTE_TO_WIFI	 = 7
};


enum ESPNotifications
{
	eNOTIFICATION_NVS_ERROR 			= 0x1,
	eNOTIFICATION_CERT_BUNDLE_REQUESTED = 0x2,
	eNOTIFICATION_NETWORK_TYPE_OVERRIDE = 0x4,
	eNOTIFICATION_MCU_WATCHDOG			= 0x8,
	eNOTIFICATION_ENERGY				= 0x10,
};

enum DiagnosticsModes
{
	eCLEAR_DIAGNOSTICS_MODE			= 0,
	eNFC_ERROR_COUNT 				= 1,
	eSWAP_COMMUNICATION_MODE 		= 2,
	eSWAP_COMMUNICATION_MODE_BACK 	= 3,
	eACTIVATE_LOGGING				= 4,
	eACTIVATE_TCP_PORT				= 5,
	eDISABLE_CERTIFICATE_ONCE		= 6,
	eDISABLE_CERTIFICATE_ALWAYS		= 7,
};

enum session_controller
{
	eCONTROLLER_MCU_STANDALONE = 1<<0,
	eCONTROLLER_ESP_STANDALONE = 1<<1,
	eCONTROLLER_ZAP_STANDALONE = 1<<2,
	eCONTROLLER_OCPP_STANDALONE = 1<<3,

	eSESSION_STANDALONE = eCONTROLLER_MCU_STANDALONE | eCONTROLLER_ESP_STANDALONE | eCONTROLLER_ZAP_STANDALONE | eCONTROLLER_OCPP_STANDALONE,
	eSESSION_ZAPTEC_CLOUD = eCONTROLLER_OCPP_STANDALONE,
	eSESSION_OCPP = eCONTROLLER_ZAP_STANDALONE | eCONTROLLER_ESP_STANDALONE,
};

struct Configuration
{
	uint32_t saveCounter;

	// Cloud settings

	uint8_t authenticationRequired; //
	//LockCableWhenConnected		//
	float currentInMaximum;
	float currentInMinimum;
	uint8_t maxPhases;
	uint8_t defaultOfflinePhase;
	float defaultOfflineCurrent;
	uint8_t isEnabled;

	// ocpp core profile settings ((commented out settings are optional AND currently not in use) OR superseded by other settings)
	// See Open charge point protocol 1.6 section 9.1 for more information

	//configurationStruct.ocpp_allow_offline_tx_for_unknown_id = false;
	//configurationStruct.ocpp_authorization_cache_enabled = false;
	char url_ocpp[URL_OCPP_MAX_LENGTH];
	bool ocpp_authorize_remote_tx_requests;
	//int configurationStruct.ocpp_blink_repeats;
	uint32_t ocpp_clock_aligned_data_interval;
	uint32_t ocpp_connection_timeout;
	//char * ocpp_connector_phase_rotation; // use phaseRotation
	uint8_t ocpp_connector_phase_rotation_max_length;
	uint8_t ocpp_get_configuration_max_keys;
	uint32_t ocpp_heartbeat_interval;
	//int ocpp_light_intensity; // use hmiBrightness instead
	bool ocpp_local_authorize_offline;
	bool ocpp_local_pre_authorize;
	//int ocpp_max_energy_on_invalid_id;
	char ocpp_meter_values_aligned_data[DEFAULT_CSL_SIZE];
	uint8_t ocpp_meter_values_aligned_data_max_length;
	char ocpp_meter_values_sampled_data[DEFAULT_CSL_SIZE];
	uint8_t ocpp_meter_values_sampled_data_max_length;
	uint32_t ocpp_meter_value_sample_interval;
	//int configurationStruct.ocpp_minimum_status_duration;
	uint8_t ocpp_number_of_connectors;
	uint8_t ocpp_reset_retries;
	bool ocpp_stop_transaction_on_ev_side_disconnect;
	bool ocpp_stop_transaction_on_invalid_id;
	char ocpp_stop_txn_aligned_data[DEFAULT_CSL_SIZE];
	uint8_t ocpp_stop_txn_aligned_data_max_length;
	char ocpp_stop_txn_sampled_data[DEFAULT_CSL_SIZE];
	uint8_t ocpp_stop_txn_sampled_data_max_length;
	char ocpp_supported_feature_profiles[128];
	uint8_t ocpp_supported_feature_profiles_max_length;
	uint8_t ocpp_transaction_message_attempts;
	uint16_t ocpp_transaction_message_retry_interval;
	bool ocpp_unlock_connector_on_ev_side_disconnect;
	//configurationStruct.web_socket_ping_interval; // Changing ping interval is not a feature of esp_websocket_client

	// ocpp local auth list profile settings
	bool ocpp_local_auth_list_enabled;
	uint16_t ocpp_local_auth_max_length;
	uint8_t ocpp_send_local_list_max_length;

	// ocpp reservation profile settings
	bool ocpp_reserve_connector_zero_supported;

	//Standalone
    char installationId[DEFAULT_STR_SIZE];
    char routingId[DEFAULT_STR_SIZE];
    char chargerName[DEFAULT_STR_SIZE];
    uint32_t diagnosticsMode;
	uint32_t transmitInterval;
	float transmitChangeLevel;
	uint32_t pulseInterval;


	// Local settings

	enum session_controller session_controller;
	uint8_t communicationMode;
	float hmiBrightness;
	uint8_t permanentLock;
	uint8_t standalone;
	uint8_t standalonePhase;
	float standaloneCurrent;
	float maxInstallationCurrentConfig;
	uint8_t phaseRotation;
	uint8_t networkType;
	uint8_t networkTypeOverride;

	char diagnosticsLog[DIAGNOSTICS_STRING_SIZE];
};


typedef enum {
    LED_STATE_OFF           = 0,
    LED_REQUESTING          = 1,


    LED_ORANGE_BLINKING     = 2,
    LED_ORANGE_CONTINUOUS   = 3,

    LED_GREEN_CONTINUOUS    = 4,

    LED_YELLOW_PULSING      = 5,
    LED_YELLOW_PULSING_FAST = 6,
    LED_YELLOW_CONTINUOUS   = 7,

    LED_BLUE_CONTINUOUS     = 8,
    LED_BLUE_PULSING        = 9,

    LED_WHITE_CONTINUOUS    = 10,
    LED_CLEAR_WHITE         = 11,
    LED_CLEAR_WHITE_BLINKING = 12,

    LED_RED                 = 13,

    LED_PURPLE_PULSE_FAST   = 14,
    LED_PURPLE_PULSE_SLOW   = 15,
    LED_MULTI_COLOR         = 16
} led_state;

typedef enum {
    SESSION_NOT_AUTHORIZED = 0,
    SESSION_AUTHORIZING = 1,
    SESSION_AUTHORIZED = 2,
} session_auth_state;

#endif /* DEVICEINFO_H_ */
