#include "esp_log.h"
#include "string.h"
#include "stdio.h"
#include "esp_sntp.h"

#include "zaptec_cloud_observations.h"

#include "ocpp_task.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_firmware_status.h"

static const char *TAG = "ota_metrics";
static time_t last_start_time = 0;
static bool is_ocpp_initiated = false;

void log_set_origin_ocpp(bool new_value){
	is_ocpp_initiated = new_value;
}
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

int log_to_ocpp(char * status){
	if(!is_ocpp_initiated)
		return 0;

	cJSON * call = ocpp_create_firmware_status_notification_request(status);
	if(call != NULL){
		enqueue_call(call, NULL, NULL, NULL, eOCPP_CALL_GENERIC);
		return 0;
	}else{
		ESP_LOGE(TAG, "Unable to create %s FirmwareStatusNotification", status);
		return -1;
	}
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
    log_to_ocpp(OCPP_FIRMWARE_STATUS_DOWNLOADING);

    return log_message(formated_message);
}

int ota_log_flash_success(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "FW validated rebooting, download time: %" PRId64 " seconds", now-last_start_time);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_DOWNLOADED);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_INSTALLING);

    return log_message(formated_message);
}

int ota_log_lib_error(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "failure after calling OTA library, failed after %" PRId64 " seconds", now-last_start_time);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_INSTALLATION_FAILED);
    return log_message(formated_message);
}

int ota_log_timeout(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "we timed out OTA after %" PRId64 " seconds, rebooting", now-last_start_time);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_DOWNLOAD_FAILED);

    return log_message(formated_message);
}


static uint32_t bytes_at_last_log = 0;
static const uint32_t progres_log_intervall = 200000;

int ota_log_download_progress_debounced(uint32_t bytes_received){
    total_bytes += bytes_received;

    if(total_bytes > (bytes_at_last_log + progres_log_intervall)){
        bytes_at_last_log = total_bytes;

        char formated_message [128];
        snprintf(formated_message, 128, "Flashed OTA data, running total=%" PRId32 "", total_bytes);
        return log_message(formated_message); 
    }

    return 1;
}

int ota_log_chunked_update_start(char *location){
    time(&last_start_time);

    char formated_message [256];
    snprintf(formated_message, 256, "starting CHUNKED FW download, location: %s", location);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_DOWNLOADING);

    return log_message(formated_message);
}

int ota_log_chunk_flashed(uint32_t start, uint32_t end, uint32_t total){
    char formated_message [128];
    snprintf(formated_message, 128, "Flashed OTA chunk from %" PRId32 " to %" PRId32 " of %" PRId32 " bytes", start, end, total);
    return log_message(formated_message); 
}

int ota_log_chunk_flash_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "failed to flash OTA chunk (%" PRId32 "), will retry chunk", error_code);
    return log_message(formated_message);
}

int ota_log_chunk_http_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "http error in chunk OTA (%" PRId32 "), will retry chunk", error_code);
    return log_message(formated_message);
}

int ota_log_chunk_validation_error(uint32_t error_code){
    char formated_message [128];
    snprintf(formated_message, 128, "failed to validate OTA chunk (%" PRId32 "), will reboot", error_code);
    return log_message(formated_message);
}

int ota_log_all_chunks_success(){
    time_t now;
    time(&now);

    char formated_message [128];
    snprintf(formated_message, 128, "CHUNKED FW validated. Rebooting soon. Total time: %" PRId64 " seconds", now-last_start_time);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_DOWNLOADED);
    log_to_ocpp(OCPP_FIRMWARE_STATUS_INSTALLING);

    return log_message(formated_message);
}
