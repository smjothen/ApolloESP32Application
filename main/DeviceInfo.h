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
	char serialNumber[10];
	char PSK[45];
	char Pin[5];
};

#endif /* DEVICEINFO_H_ */
