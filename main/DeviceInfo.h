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

#endif /* DEVICEINFO_H_ */
