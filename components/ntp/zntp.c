#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "zntp.h"
#include "../i2c/include/RTC.h"
#include "string.h"
#include "../i2c/include/i2cDevices.h"


static const char *TAG = "ZNTP           ";

static bool isSyncronized = false;
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    isSyncronized = true;
}

void zntp_init()
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_interval(86400000);//Once per day(in ms)

    // Note: Sanmina seems to block pool.ntp.org, but no.pool.ntp.org should
    // work everywhere? Otherwise we need to set this differently in production test vs. 
    // in the field.
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "no.pool.ntp.org");
    sntp_setservername(2, "europe.pool.ntp.org");

    //sntp_setserver(1,"216.239.35.12");//0xD8EF230C);// 216.239.35.12)
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    esp_sntp_init();
}

static struct tm timeinfo = { 0 };
void zntp_checkSyncStatus()
{
	    // Wait for time to be set
	    time_t now = 0;

	    int retry = 0;
	    const int retry_count = 60;
	    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
	        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d), status: %d", retry, retry_count, sntp_get_sync_status());
	        vTaskDelay(2000 / portTICK_PERIOD_MS);
	    }

	    time(&now);
	    localtime_r(&now, &timeinfo);

	    char strftime_buf[64];
	    setenv("TZ", "UTC-0", 1);
	    tzset();
	    localtime_r(&now, &timeinfo);
	    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	    ESP_LOGI(TAG, "The sensible time is: %s", strftime_buf);

	    //Set new time to be written async to RTC
	    i2cFlagNewTimeWrite();

}

bool zntp_IsSynced()
{
	return isSyncronized;
}


struct tm zntp_GetLatestNTPTime()
{
	return timeinfo;
}


void zntp_GetTimeStruct(struct tm *tmUpdatedTime)
{
	time_t nowInSeconds;

	time(&nowInSeconds);
	localtime_r(&nowInSeconds, tmUpdatedTime);
}


void zntp_GetLocalTimeZoneStruct(struct tm *tmUpdatedTime, time_t offsetSeconds)
{
	time_t nowInSeconds;

	time(&nowInSeconds);
	nowInSeconds += offsetSeconds;
	localtime_r(&nowInSeconds, tmUpdatedTime);
}

/*
 * This function returns the format required for SignedMeterValue OCMF messages.
 */
void zntp_format_time(char *buffer, time_t time_in){
	struct tm time_elements = { 0 };
	localtime_r(&time_in, &time_elements);

	setenv("TZ", "UTC-0", 1);
	tzset();
	localtime_r(&time_in, &time_elements);
	strftime(buffer, 50, "%Y-%m-%dT%H:%M:%S,000+00:00 R", &time_elements);
}

static struct tm systemTime = { 0 };
void zntp_GetSystemTime(char * buffer, time_t *now_out)
{
	time_t now = 0;

	time(&now);
	zntp_format_time(buffer, now);
	//ESP_LOGI(TAG, "The 15-min time is: %s", buffer);

	if(now_out != NULL){
		*now_out = now;
	}

}

bool zntp_GetTimeAlignementPoint(bool highInterval)
{
	time_t now = 0;

	time(&now);
	localtime_r(&now, &systemTime);

	char buffer[50];
	strftime(buffer, 50, "%Y-%m-%dT%H:%M:%S,000+00:00 R", &systemTime);

	//First check if we are within the last minute of the hour
	if(highInterval == false)
	{
		if(systemTime.tm_min < 59)
			return false;
	}

	//Check if we are within the last two seconds to have some margin
	if(systemTime.tm_sec < 58)
		return false;

	bool oneSecAway = true;
	uint8_t timeout = 21;

	while((oneSecAway == true) && (timeout > 0))
	{
		timeout--;

		time(&now);
		localtime_r(&now, &systemTime);

		if((systemTime.tm_sec == 0))
			oneSecAway = false;

		if(oneSecAway == true)
		{
			ESP_LOGW(TAG, "Syncing... %i", timeout);
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
		else
		{
			ESP_LOGW(TAG, "SYNCED!");
		}
	}

	return true;
}


bool zntp_GetTimeAlignementPointDEBUG()
{
	time_t now = 0;

	time(&now);
	localtime_r(&now, &systemTime);

	char buffer[50];
	strftime(buffer, 50, "%Y-%m-%dT%H:%M:%S,000+00:00 R", &systemTime);
	//ESP_LOGI(TAG, "The 15-min time is: %s", buffer);


	//Find correct quarterly minute
	//if(!((systemTime.tm_sec >= 58)))	//For testing

	//First check if we are within the last minute of the hour
	//if(systemTime.tm_min < 59)
	//	return false;

	//For 15-minute sync
	//if(!((systemTime.tm_sec >= 58) && ((systemTime.tm_min == 14) || (systemTime.tm_min == 29) || (systemTime.tm_min == 44) || (systemTime.tm_min == 59))))

	//Check if we are within the last two seconds to have some margin
	if((systemTime.tm_sec < 58))// || (systemTime.tm_sec < 28))
		return false;


	bool oneSecAway = true;
	uint8_t timeout = 21;

	while((oneSecAway == true) && (timeout > 0))
	{
		timeout--;

		time(&now);
		localtime_r(&now, &systemTime);

		if((systemTime.tm_sec == 0))// || (systemTime.tm_sec == 30))
			oneSecAway = false;

		if(oneSecAway == true)
		{
			ESP_LOGW(TAG, "Syncing... %i", timeout);
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
		else
		{
			ESP_LOGW(TAG, "DEBUG SYNCED!");
		}
	}

	return true;
}


void zntp_restart()
{
	sntp_restart();
}


void zntp_stop()
{
	esp_sntp_stop();
}

uint8_t zntp_enabled()
{
	return esp_sntp_enabled();
}


/*
 * This function provides the UTC time a give number of seconds into the future
 */
/*void GetUTCTimeStringWithOffset(char * timeString, uint32_t offset)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	char strftime_buf[64] = {0};

	time(&now);

	setenv("TZ", "UTC-0", 1);
	tzset();

	now += offset;

	localtime_r(&now, &timeinfo);

	struct timeval t_now;
	gettimeofday(&t_now, NULL);

	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%02m-%02dT%02H:%02M:%02S", &timeinfo);

	sprintf(strftime_buf+strlen(strftime_buf), ".%06dZ", (uint32_t)t_now.tv_usec);
	strcpy(timeString, strftime_buf);

}*/
