#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "zntp.h"


static const char *TAG = "ZNTP     ";


static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
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


void zntp_checkSyncStatus()
{
	    // Wait for time to be set
	    time_t now = 0;
	    struct tm timeinfo = { 0 };

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
	    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	    ESP_LOGI(TAG, "The sensible time is: %s", strftime_buf);
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
