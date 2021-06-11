#include "offline_log.h"

#define TAG "OFFLINE_LOG"

#include "esp_log.h"



int ensure_directory_created(){
    return 0;
}


void append_offline_energy(int timestamp, double energy){
    ESP_LOGI(TAG, "svaing offline energy %fWh@%d", energy, timestamp);
}