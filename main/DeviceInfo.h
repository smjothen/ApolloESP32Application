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

#define MAX_NR_OF_RFID_TAGS 12

struct RFIDTokens{
	char *Tag;//[37];
	int Action;
	char *ExpiryDate;//[37];
};


#define MAX_CERTIFICATE_SIZE 		50000
#define MAX_CERTIFICATE_BUNDLE_SIZE 51000

#define DEFAULT_STR_SIZE 37//Must be at least 37 for GUID! This value is also used in sscanf function!

// Network IDs
#define NETWORK_1P3W 1
#define NETWORK_3P3W 2
#define NETWORK_1P4W 3
#define NETWORK_3P4W 4

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
	eNOTIFICATION_MCU_WATCHDOG			= 0x8
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
};

#endif /* DEVICEINFO_H_ */
