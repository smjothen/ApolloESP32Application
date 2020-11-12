/*
 * DeviceInfo.h
 *
 *  Created on: 17. aug. 2020
 *      Author: vv
 */

#ifndef DEVICEINFO_H_
#define DEVICEINFO_H_

struct DeviceInfo
{
	uint8_t EEPROMFormatVersion;
	char serialNumber[10];
	char PSK[45];
	char Pin[5];
};

uint8_t GetEEPROMFormatVersion();// { return 1;}
char * GetSoftwareVersion();//char softwareVersion[8];// = "2.8.0.2";
char * GetSoftwareVersionBLE();


#define MAX_NR_OF_RFID_TAGS 20

struct RFIDTokens{
	char *Tag;//[37];
	int Action;
	char *ExpiryDate;//[37];
};

//static uint8_t GetEEPROMFormatVersion() { return 1;}
//static char softwareVersion[] = "2.8.0.2";

#define DEFAULT_STR_SIZE 37//Must be at least 37 for GUID! This value is also used in sscanf function!

//Numbers should match Pro
enum CommunicationMode
{
	eCONNECTION_NONE 		 = 0,
	eCONNECTION_WIFI		 = 1,
	eCONNECTION_LTE			 = 5,
	eCONNECTION_LTE_TO_WIFI	 = 7
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

};

#endif /* DEVICEINFO_H_ */
