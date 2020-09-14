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

static uint8_t GetEEPROMFormatVersion() { return 1;}

static char softwareVersion[] = "0.0.0.2";

enum ConnectionInterface
{
	eCONNECTION_NO_INTERFACE = 0,
	eCONNECTION_WIFI		 = 1,
	eCONNECTION_4G			 = 2,
	eCONNECTION_4G_TO_WIFI	 = 3
};

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

#endif /* DEVICEINFO_H_ */
