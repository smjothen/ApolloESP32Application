#include "esp_log.h"
#include "string.h"
#include "cJSON.h"

#include "device_twin.h"

#define TAG "device_twin"

int on_device_twin_message(char * payload){
    ESP_LOGI(TAG, "updating device settings with %s", payload);
    cJSON* twin_data = cJSON_Parse(payload);

    if(cJSON_HasObjectItem(twin_data, "desired")){
        cJSON* desired_twin_data = cJSON_GetObjectItem(twin_data, "desired");

        if(cJSON_HasObjectItem(desired_twin_data, "Settings")){
            cJSON* settings = cJSON_GetObjectItem(desired_twin_data, "Settings");

            if(cJSON_HasObjectItem(settings, "120")){
                char *auth_required = cJSON_GetObjectItem(settings, "120")->valuestring;
                if(strcmp(auth_required, "0")==0){
                    ESP_LOGI(TAG, "Authentication is not required by device twin settings");
                }else{
                    ESP_LOGW(TAG, "Authentication is enabled by device twin but not implmented yet");
                }
            }

            if(cJSON_HasObjectItem(settings, "711")){
                char *is_enabled = cJSON_GetObjectItem(settings, "711")->valuestring;
                if(strcmp(is_enabled, "1")==0){
                    ESP_LOGI(TAG, "chargepoint is ENabled by twin settings");
                }else{
                    ESP_LOGW(TAG, "chargepoint is DISabled by twin settings");
                }
            }

            if(cJSON_HasObjectItem(settings, "802")){
                char *charge_point_name = cJSON_GetObjectItem(settings, "802")->valuestring;
                ESP_LOGI(TAG, "this charger has been named \"%s\"", charge_point_name);
            }


        }else{
            ESP_LOGW(TAG, "no settings in device twin payload");
        }
    }else{
        ESP_LOGW(TAG, "no desired field in device twin payload");
    }

    cJSON_Delete(twin_data);
    return 0;
}