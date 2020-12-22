#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "freertos/event_groups.h"
#include "string.h"

#include "apollo_ota.h"
#include "ota_location.h"
#include "pic_update.h"
#include "ota_log.h"

#define TAG "OTA"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");


static EventGroupHandle_t event_group;
static const int OTA_UNBLOCKED = BIT0;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED, setting debug header");
        esp_http_client_set_header(evt->client, "Zaptec-Debug-Info", "apollo/ota/arnt/1");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ota_log_download_progress_debounced(evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}


static void ota_task(void *pvParameters){

    char image_location[1024] = {0};
    esp_http_client_config_t config = {
        .url = image_location,
        .cert_pem = (char *)server_cert_pem_start,
        // .use_global_ca_store = true,
        .event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = 1536,
    };

    // config.skip_cert_common_name_check = true;

    while (true)
    {
    	size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM1: DRAM: %i Lo: %i", free_dram, low_dram);

        ESP_LOGI(TAG, "waiting for ota event");
        //xEventGroupWaitBits(event_group, OTA_UNBLOCKED, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGW(TAG, "attempting ota update");
        ota_log_location_fetch();

        get_image_location(image_location,sizeof(image_location));
        // strcpy( image_location,"http://api.zaptec.com/api/firmware/6476103f-7ef9-4600-9450-e72a282c192b/download");
        // strcpy( image_location,"https://api.zaptec.com/api/firmware/ZAP000001/current");
        ESP_LOGI(TAG, "image location to use: %s", image_location);


    	free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM2: DRAM: %i Lo: %i", free_dram, low_dram);

        ota_log_download_start(image_location);
        esp_err_t ret = esp_https_ota(&config);
        if (ret == ESP_OK) {
            ota_log_flash_success();


            // give the system some time to finnish sending the log message
            // a better solution would be to detect the message sent event, 
            // though one must ensure there is a timeout, as the system NEEDS a reboot now
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            ota_log_lib_error();
        }

    	free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM3: DRAM: %i Lo: %i", free_dram, low_dram);

        vTaskDelay(30000 / portTICK_RATE_MS);
    }
}

void validate_booted_image(void){
    const esp_partition_t * partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Checking if VALID on partition %s ", partition->label);

    int dspic_update_success = update_dspic();

    if(dspic_update_success<0){
            ESP_LOGE(TAG, "FAILED to update dsPIC, restarting now...");
            // We failed to bring the dsPIC app to the version embedded in this code
            // On next reboot we will roll back, and the old dsPIC app will be flashed
            // TODO: should we restart now?
            // TODO: send info to Cloud before restart.
            esp_restart();
    }

    esp_ota_img_states_t ota_state;
    esp_err_t ret = esp_ota_get_state_partition(partition, &ota_state);

    if(ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_LOGI(TAG, "we booted a new image, lets make sure the dsPIC has the FW from this image");
        if(dspic_update_success<0){
            // could we use other error handeling here? Or should everything be handeled above?
        }else{
            ret = esp_ota_mark_app_valid_cancel_rollback();
            if(ret != ESP_OK){
                ESP_LOGE(TAG, "marking partition as valid failed with: %d", ret);
            }else{
                ESP_LOGI(TAG, "partition marked as valid");
            }

        }
        
    }
    else
    {
        ESP_LOGI(TAG, "partition already valid");
    }
}



void start_ota_task(void){
    ESP_LOGI(TAG, "starting ota task");

    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);
    // esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    // esp_log_level_set("esp_ota_ops", ESP_LOG_DEBUG);
    // esp_log_level_set("MQTT_CLIENT", ESP_LOG_INFO);

    event_group = xEventGroupCreate();
    xEventGroupClearBits(event_group,OTA_UNBLOCKED);

    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096*2;
    xTaskCreate( 
        ota_task, "otatask", stack_size, 
        &ucParameterToPass, 7, &taskHandle
    );
    ESP_LOGD(TAG, "...");
}

int start_ota(void){
    xEventGroupSetBits(event_group, OTA_UNBLOCKED);
    return 0;
}


const char* OTAReadRunningPartition()
{
	const esp_partition_t * partition = esp_ota_get_running_partition();
	//ESP_LOGW(TAG, "Partition name: %s", partition->label);

	return partition->label;
}
