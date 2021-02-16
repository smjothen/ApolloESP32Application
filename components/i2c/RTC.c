
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "i2cInterface.h"
#include "RTC.h"

#include <time.h>
#include <sys/time.h>

static const char *TAG_RTC = "RTC    ";

/// Driver for the RTC: PCF85063A
/// https://www.nxp.com/products/peripherals-and-logic/signal-chain/real-time-clocks/rtcs-with-ic-bus/tiny-real-time-clock-calendar-with-alarm-function-and-ic-bus:PCF85063A

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

	// NB! Month nr in RTC is one higher than in tm struct
	uint8_t month = newTime.tm_mon + 1;

	//Input value of 120 = (2020 - 1900)
	//Save value 20 = (2020 - 2000)
	uint8_t year = newTime.tm_year;
	if(year > 100)
		year -= 100;

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

	// Highest bits can be undefined or control bits,
	// Make sure to mask them out before converting time.
	readBytes[0] = readBytes[0] & ~0x80;	//Sec - Clear OS bit 7
	readBytes[1] = readBytes[1] & ~0x80;	//Min - Clear unused bit 7
	readBytes[2] = readBytes[2] & ~0xE0;	//Hours - Clear unused bit 7, 6 and 5(AM/PM)
	readBytes[3] = readBytes[3] & ~0xC0;	//Days - Clear unused bit 7
	//readBytes[4] = readBytes[4] & ~0xF8;	//Weekdays - not used
	readBytes[5] = readBytes[5] & ~0xE0;	//Month - Clear unused bit 7,6,5
	//readBytes[6] = readBytes[6];			//Year - Uses all bits, not masking


	RTCtime.tm_sec = BcdToDec(readBytes[0]);
	RTCtime.tm_min = BcdToDec(readBytes[1]);
	RTCtime.tm_hour = BcdToDec(readBytes[2]);
	RTCtime.tm_mday = BcdToDec(readBytes[3]);

	//No need to read weekday (readBytes[4])

	// NB! Month nr in RTC is one higher than in tm struct
	RTCtime.tm_mon = BcdToDec(readBytes[5]);
	if(RTCtime.tm_mon > 0)
		RTCtime.tm_mon -= 1;

	//Saved value (2020 - 2000) = 20;
	//Output value of 120 = (2020 - 1900)
	RTCtime.tm_year = BcdToDec(readBytes[6]);
	if((RTCtime.tm_year != 0) && (RTCtime.tm_year < 100))
		RTCtime.tm_year += 100;

	return RTCtime;
}


void RTCSoftwareReset()
{
	uint8_t writeBytes[8] = {0};

	writeBytes[0] = 0x00;
	writeBytes[1] = 0x58;
	i2c_master_write_slave(slaveAddressRTC, (uint8_t*)&writeBytes, 2);
}


bool RTCReadAndUseTime()
{
	bool RTCvalid = false;

	//struct tm writeTime = {0};
	//strptime("2020-09-23 05:01:02", "%Y-%m-%d %H:%M:%S", &writeTime);

	//RTCWriteTime(writeTime);

	struct tm readTime = {0};

	//RTCSoftwareReset(); //resets to 2000-01-01 00:00:00
	//vTaskDelay(100 / portTICK_RATE_MS);

	readTime = RTCReadTime();
	char buffer[26];
	strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", &readTime);

	ESP_LOGW(TAG_RTC, "**********RTC time  %s ********", buffer);

	if(readTime.tm_year < 19)
	{
		RTCvalid = false;
		//Must wait for NTP connection to get valid time
		ESP_LOGE(TAG_RTC, "RTC time is invalid: Year == %d", readTime.tm_year);
	}
	else
	{
		RTCvalid = true;

		time_t epochSec = mktime(&readTime);

		ESP_LOGW(TAG_RTC, "**********epocSec: %ld", epochSec);

		struct timeval tv = {0};
		tv.tv_sec = epochSec;
		int ret = settimeofday(&tv, NULL);

		ESP_LOGW(TAG_RTC, "RTC time set as System time: %s, ret: %d", buffer, ret);

		// Must read back using gettimeofday() verify correct time
		struct timeval tvRead = {0};
		gettimeofday(&tvRead, NULL);

		ESP_LOGW(TAG_RTC, "r: %ld, w: %ld, diff: %ld", tvRead.tv_sec, tv.tv_sec, tvRead.tv_sec-tv.tv_sec);
	}

	return RTCvalid;
}

void RTCTestTime()
{
	struct tm writeTime = {0};
	struct tm readTime = {0};


	readTime = RTCReadTime();
	char buffer[26];
	strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", &readTime);

	ESP_LOGW(TAG_RTC, "**********  %s ********", buffer);

	int count = 0;

	while(count < 6)
	{
		if(count == 0)
			writeTime = readTime;
		if(count == 1)
			writeTime.tm_sec = 58;
		if(count == 2)
		{
			writeTime.tm_sec = 58;
			writeTime.tm_min = 59;
		}
		if(count == 3)
		{
			writeTime.tm_sec = 58;
			writeTime.tm_min = 59;
			writeTime.tm_hour = 23;
		}
		if(count == 4)
		{
			writeTime.tm_sec = 58;
			writeTime.tm_min = 59;
			writeTime.tm_hour = 23;
			writeTime.tm_mday = 31;
		}
		if(count == 5)
		{
			writeTime.tm_sec = 58;
			writeTime.tm_min = 59;
			writeTime.tm_hour = 23;
			writeTime.tm_mday = 31;
			writeTime.tm_mon = 11;
		}
		if(count == 6)
		{
			writeTime.tm_sec = 58;
			writeTime.tm_min = 59;
			writeTime.tm_hour = 23;
			writeTime.tm_mday = 31;
			writeTime.tm_mon = 11;
			writeTime.tm_year = 19;
		}
		count++;

		char writebuffer[26];
		strftime(writebuffer, 26, "%Y-%m-%d %H:%M:%S", &writeTime);

		ESP_LOGW(TAG_RTC, " ");
		ESP_LOGW(TAG_RTC, "Written **********  %s ********", writebuffer);
		RTCWriteTime(writeTime);

		vTaskDelay(4000 / portTICK_RATE_MS);

		readTime = RTCReadTime();
		char buffer[26];
		strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", &readTime);

		ESP_LOGW(TAG_RTC, "Read   **********  %s ********", buffer);
		ESP_LOGW(TAG_RTC, " ");

		vTaskDelay(1000 / portTICK_RATE_MS);
	}
}
