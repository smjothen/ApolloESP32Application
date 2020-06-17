#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "zaptec_cloud_listener.h"
#include "sas_token.h"

#define TAG "Cloud Listener"

void start_cloud_listener_task(void){
    ESP_LOGI(TAG, "Connecting to IotHub");

    char token[64];
    create_sas_token(600, &token);
    ESP_LOGE(TAG, "connedtion token is %s", token);
}