#include "ota_command.h"


#include "esp_log.h"

#include "apollo_ota.h"
#include "ble_interface.h"

#define TAG "ota_command"

bool ota_is_allowed(void){
    // TODO:
    // check pilot signal
    // ...
    return true;
}

int before_ota(void){
    // TODO:
    // stop periodic communication with MCU
    // stop i2c
    // dissable new charging sessions
    // ...

	ble_interface_deinit();
    return 0;
}


int _on_ota_command(bool forced){
    if((forced==true)||(ota_is_allowed()==true)){
        ESP_LOGD(TAG, "Preparing system for OTA");
        if(before_ota()==0){
            ESP_LOGD(TAG, "Starting OTA client");
            return start_ota();
        }else{
            ESP_LOGI(TAG, "failed to prepare system for OTA");
            return -1;
        }
    }

    ESP_LOGI(TAG, "The system can not perform OTA at the current time");
    return -1;
}

int on_ota_command(void){
    return _on_ota_command(false);
}
int on_forced_ota_command(void){
    return _on_ota_command(true);
}