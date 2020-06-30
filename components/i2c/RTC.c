
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "i2cInterface.h"
#include "RTC.h"


static uint8_t slaveAddressRTC = 0x51;


static int  DecToBcd(int dec)
{
   return (((dec/10) << 4) | (dec % 10));
}

static int BcdToDec(int bcd)
{
   return (((bcd>>4)*10) + (bcd & 0xF));
}


esp_err_t RTCWriteTime(struct tm newTime)
{
	uint8_t writeBytes[8] = {0};

	writeBytes[0] = 0x00;
	writeBytes[1] = 0x00;
	i2c_master_write_slave(slaveAddressRTC, (uint8_t*)&writeBytes, 2);

	uint8_t seconds = newTime.tm_sec;
	uint8_t minutes = newTime.tm_min;
	uint8_t hours = newTime.tm_hour;
	uint8_t day = newTime.tm_mday;
	//uint8_t weekday = 0;	//We don't use weekday info
	uint8_t month = newTime.tm_mon;
	uint8_t year = newTime.tm_year;

	//	seconds = 40;
	//	minutes = 59;
	//	hours = 23;
	//	day = 28;
	//	weekday = 0;
	//	month = 02;
	//	year = 21;

	//0x00 - CONFIG 0
	writeBytes[0] = 0x04; //Register address

	//0x04 - SECONDS
	writeBytes[1] = DecToBcd(seconds);

	//0x05 - MINUTES
	writeBytes[2] = DecToBcd(minutes);

	//0x06 - HOURS(Bit 5: 0 = 24h, 1 = 12h mode)
	writeBytes[3] = DecToBcd(hours);

	//0x07 - DAYS
	writeBytes[4] = DecToBcd(day);

	//0x08 - WEEKDAY - NOT USED
	writeBytes[5] = 0x0;

	//0x09 - MONTH
	writeBytes[6] = DecToBcd(month);

	//0x0A - YEAR
	writeBytes[7] = DecToBcd(year);

	esp_err_t err = i2c_master_write_slave(slaveAddressRTC, (uint8_t*)&writeBytes, 8);

	return err;
}


struct tm RTCReadTime()
{
	struct tm RTCtime = {0};

	uint8_t readBytes[7] = {0};

	uint8_t readreg = 4;
	i2c_master_read_slave_at_address(slaveAddressRTC, readreg, readBytes, 7);

	RTCtime.tm_sec = BcdToDec(readBytes[0]);
	RTCtime.tm_min = BcdToDec(readBytes[1]);
	RTCtime.tm_hour = BcdToDec(readBytes[2]);

	RTCtime.tm_mday = BcdToDec(readBytes[3]);
	//To no read weekday (readBytes[4])
	RTCtime.tm_mon = BcdToDec(readBytes[5]);
	RTCtime.tm_year = BcdToDec(readBytes[6]);

	return RTCtime;
}

