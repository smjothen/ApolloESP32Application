/*
 * DeviceInfo.h
 *
 *  Created on: 17. aug. 2020
 *      Author: vv
 */

#ifndef DEVICEINFO_H_
#define DEVICEINFO_H_

#define ENABLE_LOGGING	//default commented out
//#define DEVELOPEMENT_URL	//default commented out
//#define RUN_FACTORY_ASSIGN_ID //default commented out
//#define RUN_FACTORY_TESTS //default commented out

//#define MCU_APP_ONLY

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



typedef enum {
    HW_SPEED_UNKNOWN = 0,
    HW_SPEED_1       = 1,
    HW_SPEED_3_UK    = 3,
} hw_speed_revision;


typedef enum {
    HW_POWER_UNKNOWN 	= 0,
    HW_POWER_1      	= 1,
    HW_POWER_2      	= 2,
    HW_POWER_3_UK   	= 3,
    HW_POWER_4_X804  	= 4,
    HW_POWER_5_UK_X804 	= 5,
} hw_power_revision;


#define MAX_CERTIFICATE_SIZE 		50000
#define MAX_CERTIFICATE_BUNDLE_SIZE 51000

#define DEFAULT_STR_SIZE 37//Must be at least 37 for GUID! This value is also used in sscanf function!
#define PREFIX_GUID 41
#define SCHEDULE_SIZE 196	//(14*14) -> ((14*13) + (13 + \0)) = 196
#define DIAGNOSTICS_STRING_SIZE 100

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

#define DEFAULT_MAX_CHARGE_DELAY 600

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
	//Standalone
    char installationId[DEFAULT_STR_SIZE];
    char routingId[DEFAULT_STR_SIZE];
    char chargerName[DEFAULT_STR_SIZE];
    uint32_t diagnosticsMode;
	uint32_t transmitInterval;
	float transmitChangeLevel;
	uint32_t pulseInterval;


	// Local settings

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

	char location[4];//3 letters + EOL
	char timezone[DEFAULT_STR_SIZE];
	//uint8_t dstUsage;
	//uint8_t useSchedule;
	char timeSchedule[SCHEDULE_SIZE];
	uint32_t maxStartDelay;
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
