#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "zntp.h"
#include "../i2c/include/RTC.h"
#include "string.h"
#include "../i2c/include/i2cDevices.h"


static const char *TAG = "ZNTP     ";

static bool isSyncronized = false;
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    isSyncronized = true;
}

void zntp_init()
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_interval(86400000);//Once per day(in ms)
    sntp_setservername(0, "pool.ntp.org");
    //sntp_setserver(1,"216.239.35.12");//0xD8EF230C);// 216.239.35.12)
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
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
	ESP_LOGI(TAG, "The 15-min time is: %s", buffer);

	if(now_out != NULL){
		*now_out = now;
	}

}

bool zntp_Get15MinutePoint()
{
	time_t now = 0;

	time(&now);
	localtime_r(&now, &systemTime);

	char buffer[50];
	strftime(buffer, 50, "%Y-%m-%dT%H:%M:%S,000+00:00 R", &systemTime);
	ESP_LOGI(TAG, "The 15-min time is: %s", buffer);


	//Find correct quarterly minute
	//if(!((systemTime.tm_sec >= 58)))	//For testing

	if(!((systemTime.tm_sec >= 58) && ((systemTime.tm_min == 14) || (systemTime.tm_min == 29) || (systemTime.tm_min == 44) || (systemTime.tm_min == 59))))
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
			ESP_LOGW(TAG, "...looking %i", timeout);
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
		else
		{
			ESP_LOGI(TAG, "...found");
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
	sntp_stop();
}

uint8_t zntp_enabled()
{
	return sntp_enabled();
}
