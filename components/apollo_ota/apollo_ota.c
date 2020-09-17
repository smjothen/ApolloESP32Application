#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "freertos/event_groups.h"

#include "apollo_ota.h"
#include "ota_location.h"
#include "pic_update.h"

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
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        // ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len); to much noice
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

    update_dspic();

    char image_location[256] = {0};
    esp_http_client_config_t config = {
        .url = image_location,
        .cert_pem = (char *)server_cert_pem_start,
        // .use_global_ca_store = true,
        .event_handler = _http_event_handler,
    };

    // config.skip_cert_common_name_check = true;

    while (true)
    {
        ESP_LOGI(TAG, "waiting for ota event");
        xEventGroupWaitBits(event_group, OTA_UNBLOCKED, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGW(TAG, "attempting ota update");

        get_image_location(image_location,sizeof(image_location));
        // strcpy( image_location,"http://api.zaptec.com/api/firmware/6476103f-7ef9-4600-9450-e72a282c192b/download");
        // strcpy( image_location,"https://api.zaptec.com/api/firmware/ZAP000001/current");
        ESP_LOGI(TAG, "image location to use: %s", image_location);

        esp_err_t ret = esp_https_ota(&config);
        if (ret == ESP_OK) {
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Firmware upgrade failed");
        }

        vTaskDelay(3000 / portTICK_RATE_MS);
    }
}

void validate_booted_image(void){
    const esp_partition_t * partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Checking if VALID on partition %s ", partition->label);

    esp_ota_img_states_t ota_state;
    esp_err_t ret = esp_ota_get_state_partition(partition, &ota_state);

    if(ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ret = esp_ota_mark_app_valid_cancel_rollback();
        if(ret != ESP_OK){
             ESP_LOGE(TAG, "marking partition as valid failed with: %d", ret);
        }else{
            ESP_LOGI(TAG, "partition marked as valid");
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
        &ucParameterToPass, 5, &taskHandle
    );
    ESP_LOGD(TAG, "...");
}

int start_ota(void){
    xEventGroupSetBits(event_group, OTA_UNBLOCKED);
    return 0;
}