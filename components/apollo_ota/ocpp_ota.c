#include "ocpp_ota.h"

#include "esp_ota_ops.h"

#include "stdio.h"
#include "string.h"
#include "freertos/task.h"

#include "ota_log.h"
#include "esp_log.h"
#include "esp_event.h"
#include "certificate.h"
#include <math.h>

#include "types/ocpp_firmware_status.h"

static const char *TAG = "OCPP OTA       ";
static esp_https_ota_handle_t ota_handle = NULL;
static void (*status_cb)(const char * status) = NULL;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
		break;
	case HTTP_EVENT_ON_CONNECTED:
		ESP_LOGW(TAG, "HTTP_EVENT_ON_CONNECTED");
		break;
	case HTTP_EVENT_HEADER_SENT:
		ESP_LOGW(TAG, "HTTP_EVENT_HEADER_SENT");
		break;
	case HTTP_EVENT_ON_HEADER:
		ESP_LOGW(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
		break;
	case HTTP_EVENT_ON_DATA:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA %d", evt->data_len);
		ota_log_download_progress_debounced(evt->data_len);
		break;
	case HTTP_EVENT_ON_FINISH:
		ESP_LOGW(TAG, "HTTP_EVENT_ON_FINISH");
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
		break;
	case HTTP_EVENT_REDIRECT:
		ESP_LOGW(TAG, "HTTP_EVENT_REDIRECT");
		break;
	}
	return ESP_OK;
}

static void ota_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data){
	switch (event_id) {
	case ESP_HTTPS_OTA_START:
		ESP_LOGW(TAG, "ESP_HTTPS_OTA_START");
		break;
	case ESP_HTTPS_OTA_CONNECTED:
		ESP_LOGD(TAG, "ESP_HTTPS_OTA_CONNECTED");
		break;
	case ESP_HTTPS_OTA_GET_IMG_DESC:
		ESP_LOGD(TAG, "ESP_HTTPS_OTA_GET_IMG_DESC");
		break;
	case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
		if(*(esp_chip_id_t *)event_data != ESP_CHIP_ID_ESP32){
			ESP_LOGE(TAG, "Expected OTA app to be for ESP_32 chip");
			do_ocpp_ota_abort();
		}
		break;
	case ESP_HTTPS_OTA_DECRYPT_CB:
		ESP_LOGD(TAG, "ESP_HTTPS_OTA_DECRYPT_CB");
		break;
	case ESP_HTTPS_OTA_WRITE_FLASH:
		ESP_LOGD(TAG, "ESP_HTTPS_OTA_WRITE_FLASH: %d written", *(int *)event_data);
		break;
	case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
		ESP_LOGW(TAG, "ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION: %d next", *(esp_partition_subtype_t *)event_data);
		break;
	case ESP_HTTPS_OTA_FINISH:
		ESP_LOGW(TAG, "ESP_HTTPS_OTA_FINISH");
		break;
	case ESP_HTTPS_OTA_ABORT:
		ESP_LOGW(TAG, "ESP_HTTPS_OTA_ABORT");
		break;
	}
}

static bool doAbortOTA = false;
void do_ocpp_ota_abort()
{
	if(ota_handle != NULL){
		ESP_LOGE(TAG, "Attemting to aborting OCPP OTA. Result: %s", esp_err_to_name(esp_https_ota_abort(ota_handle)));
	}else{
		ESP_LOGE(TAG, "No active OCPP OTA to abort");
	}
}

static size_t request_size = 8192;
void ota_set_ocpp_request_size(size_t request_size)
{
	request_size = request_size;
	ESP_LOGW(TAG, "New chuck size: %zu", request_size);
}

esp_err_t ocpp_ota_validate_app_description(const esp_app_desc_t * app_description){
	if(strncmp(app_description->project_name, "ApolloEsp32Application", 32) != 0){
		ESP_LOGE(TAG, "OTA got unexpected app name: %.32s, expected 'ApolloEsp32Application'", app_description->project_name);
		return ESP_FAIL;
	}

	long version = strtol(app_description->version, NULL, 10);
	if(version < 2){
		ESP_LOGE(TAG, "OCPP OTA disallows major version below 2. Got firmware version %.32s", app_description->version);
		return ESP_FAIL;
	}

	return ESP_OK;
}

void do_ocpp_ota(char *image_location, void (*status_update_cb)(const char * status)){
	status_cb = status_update_cb;
	bool useCert = certificate_GetUsage();

	if(!useCert)
		ESP_LOGE(TAG, "CERTIFICATES NOT USED");

	esp_http_client_config_t http_config = {
		.url = image_location,
		.use_global_ca_store = useCert,
		.event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = 4096,
		.keep_alive_enable = true,
	};

	esp_err_t err = esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, NULL);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to attach ota event handler: %s", esp_err_to_name(err));
	}

	esp_https_ota_config_t ota_config = {
		.http_config = &http_config,
		.partial_http_download = false, // True requires support for http HEAD and more on server side
		.max_http_request_size = request_size,
	};

	ota_log_download_start(image_location);
	if(status_cb != NULL)
		status_cb(OCPP_FIRMWARE_STATUS_DOWNLOADING);

	err = esp_https_ota_begin(&ota_config, &ota_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Unable to begin https ota: %s", esp_err_to_name(err));
		goto error;
	}

	esp_app_desc_t app_desc;
	err = esp_https_ota_get_img_desc(ota_handle, &app_desc);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "OTA unable to get app description");
		goto error;
	}

	err = ocpp_ota_validate_app_description(&app_desc);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Rejecting OTA due to invalid app description: %s", esp_err_to_name(err));
		goto error;
	}

	while(true){
		err = esp_https_ota_perform(ota_handle);
		if(err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
			break;

		if(doAbortOTA){
			ESP_LOGW(TAG, "Aborting OCPP OTA");
			goto error;
		}

		ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(ota_handle));
	}

	if(err != ESP_OK || esp_https_ota_is_complete_data_received(ota_handle) != true) {
		ESP_LOGE(TAG, "OTA no longer in progress but complete data not received: %s", esp_err_to_name(err));
		goto error;
	}

	err = esp_https_ota_finish(ota_handle);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "OTA failed during finishing: %s", esp_err_to_name(err));
		goto error;
	}

	ESP_LOGI(TAG, "OCPP_OTA Complete, Rebooting...");

	ota_log_flash_success();
	if(status_cb != NULL){
		status_cb(OCPP_FIRMWARE_STATUS_DOWNLOADED);
		status_cb(OCPP_FIRMWARE_STATUS_INSTALLED);
	}

	vTaskDelay(pdMS_TO_TICKS(3000));
	esp_restart();

error:
	ota_log_lib_error();
	if(status_cb != NULL)
		status_cb(OCPP_FIRMWARE_STATUS_DOWNLOAD_FAILED);

	esp_https_ota_abort(ota_handle);
	ota_handle = NULL;

	return;
}
