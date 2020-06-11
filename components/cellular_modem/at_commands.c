#include "esp_log.h"
#include <string.h> 

#include "at_commands.h"
#include "ppp_task.h"

#define TAG "AT COMMAND"

int at_command_at(void){
    ESP_LOGI(TAG, "running AT");
    char at_buffer[LINE_BUFFER_SIZE];

    send_line("AT");
    int result = await_line(at_buffer, pdMS_TO_TICKS(800));
    if(pdPASS==result){
        ESP_LOGI(TAG, "AT result: %s", at_buffer);
        if(strstr(at_buffer, "OK")){
            return 0;
        }
        return -1;
    }else{
        ESP_LOGI(TAG, "command failed");
        return -2;
    }
    return -3;
}