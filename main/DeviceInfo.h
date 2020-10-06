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

//static uint8_t GetEEPROMFormatVersion() { return 1;}
//static char softwareVersion[] = "2.8.0.2";

enum ConnectionInterface
{
	eCONNECTION_NONE 		 = 0,
	eCONNECTION_WIFI		 = 1,
	eCONNECTION_LTE			 = 2,
	eCONNECTION_LTE_TO_WIFI	 = 3
};

struct Configuration
{
	uint32_t saveCounter;

	uint8_t authenticationRequired;
	uint32_t transmitInterval;
	float transmitChangeLevel;

	uint8_t communicationMode;
	float hmiBrightness;
	uint8_t permanentLock;

	uint8_t standalone;
	uint8_t standalonePhase;
	float standaloneCurrent;
	float maxInstallationCurrentConfig;
	uint8_t maxPhases;
	uint8_t phaseRotation;
	uint8_t networkType;
};

#endif /* DEVICEINFO_H_ */
