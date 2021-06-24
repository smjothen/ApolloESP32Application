#include "esp_log.h"
#include "string.h"
#include "stdio.h"
#include "esp_sntp.h"

#include "zaptec_cloud_observations.h"

static const char *TAG = "ota_metrics";
static time_t last_start_time = 0;

int log_message(char *msg){
    time_t now = 0;
    struct tm timeinfo = { 0 };

    time(&now);
    localtime_r(&now, &timeinfo);

    char time_string[64];
    strftime(time_string, sizeof(time_string), "@{%Y-%m-%d %H:%M:%S}", &timeinfo);

    //Use LOGW to highlight
    ESP_LOGW(TAG, "%s %s",time_string, msg);

    char formated_message [256];
    snprintf(formated_message, 256, "[%s] %s %s", TAG, time_string, msg);
     
    return publish_debug_message_event(formated_message, cloud_event_level_information);
}

int ota_log_location_fetch(){
    return log_message("finding ota file location");
}

static uint32_t total_bytes = 0;
int ota_log_download_start(char *location){
    time(&last_start_time);
    total_bytes = 0;

    char formated_message [256];
    snprintf(formated_message, 256, "starting FW download, location: %s", location);
    return log_message(formated_message);
}

int ota_log_flash_success(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "FW validated rebooting, download time: %ld seconds", now-last_start_time);
    return log_message(formated_message);
}

int ota_log_lib_error(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "failure after calling OTA library, failed after %ld seconds", now-last_start_time);
    return log_message(formated_message);
}

int ota_log_timeout(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "we timed out OTA after %ld seconds, rebooting", now-last_start_time);
    return log_message(formated_message);
}


static uint32_t bytes_at_last_log = 0;
static const uint32_t progres_log_intervall = 200000;

int ota_log_download_progress_debounced(uint32_t bytes_received){
    total_bytes += bytes_received;

    if(total_bytes > (bytes_at_last_log + progres_log_intervall)){
        bytes_at_last_log = total_bytes;

        char formated_message [128];
        snprintf(formated_message, 128, "Flashed OTA data, running total=%d", total_bytes);
        return log_message(formated_message); 
    }

    return 1;
}

int ota_log_chunked_update_start(char *location){
    time(&last_start_time);

    char formated_message [256];
    snprintf(formated_message, 256, "starting CHUNKED FW download, location: %s", location);
    return log_message(formated_message);
}

int ota_log_chunk_flashed(uint32_t start, uint32_t end, uint32_t total){
    char formated_message [128];
    snprintf(formated_message, 128, "Flashed OTA chunk from %d to %d of %d bytes", start, end, total);
    return log_message(formated_message); 
}

int ota_log_chunk_flash_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "failed to flash OTA chunk (%d), will retry chunk", error_code);
    return log_message(formated_message);
}

int ota_log_chunk_http_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "http error in chunk OTA (%d), will retry chunk", error_code);
    return log_message(formated_message);
}

int ota_log_chunk_validation_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "failed to validate OTA chunk (%d), will reboot", error_code);
    return log_message(formated_message);
}

int ota_log_all_chunks_success(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "CHUNKED FW validated. Rebooting soon. Total time: %ld seconds", now-last_start_time);
    return log_message(formated_message);
}
