#include "esp_log.h"
#include "string.h"
#include <stdio.h>
#include "cJSON.h"

#include "device_methods.h"
#include "zaptec_cloud_listener.h"

#define TAG "device_methods"

int on_method_call(char* topic, char* payload){
    ESP_LOGD(TAG, "handleing call %s with arg %s", topic, payload);
    // topic example: $iothub/methods/POST/Hallo/?$rid=1

    char mutable_topic[256];
    char reply_topic[256];
    strncpy(mutable_topic, topic, 256);
    const char delimiter[] = "/";
    char* method_name;
    char* result_key;

    char* token = strtok(mutable_topic, delimiter);

    for(int i = 0; i<5; i++){
        if(token == NULL){
            ESP_LOGW(TAG, "could not parse method topic");
            return -1;
        }

        if(i==3){
            method_name = token;
        }else if(i==4){
            result_key = token;
        }else{
            ESP_LOGD(TAG, "skipping topic elem %d (%s)", i, token);
        }

        token = strtok(NULL, delimiter);
    }

    ESP_LOGI(TAG, "method topic parse results %s and %s", method_name, result_key);

    if(strlen(payload)>0){
        cJSON * parsed_payload = cJSON_Parse(payload);
        if (parsed_payload == NULL){
            ESP_LOGW(TAG, "Failed to parse method payload");
        }else{
            ESP_LOGI(TAG, "Payload type is %d", parsed_payload->type);
        }
    }else{
        ESP_LOGW(TAG, "no payload for method call");
    }

    snprintf(reply_topic, 256, "$iothub/methods/res/200/%s", result_key); 
    ESP_LOGI(TAG, "replying on %s", reply_topic);
    publish_to_iothub(NULL, reply_topic);

    return 0;
}