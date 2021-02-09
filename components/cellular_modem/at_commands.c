#include "esp_log.h"
#include <string.h> 

#include "at_commands.h"
#include "ppp_task.h"

#define TAG "AT COMMAND"

static int at_command_with_ack_and_lines(char * command, char * success_key, uint32_t timeout_ms, uint32_t line_count){
    char at_buffer[LINE_BUFFER_SIZE] = {0};
    ESP_LOGD(TAG, "Sending {%s}", command);
    send_line(command); 

    for(int line = 0; line<line_count; line ++){
        int result = await_line(at_buffer, pdMS_TO_TICKS(timeout_ms));
        if(pdPASS==result){
            ESP_LOGI(TAG, "AT result: %s", at_buffer);
            if(strstr(at_buffer, success_key)){
                return 0;
            }
        }else{
            ESP_LOGW(TAG, "command [%s] timeout", command);
            return -2;
        }
    }

    ESP_LOGW(TAG, "command [%s] not ok. last we got was [%s]", command, at_buffer);
    return -1;
}

int at_command_with_ok_ack(char * command, uint32_t timeout_ms){
    return at_command_with_ack_and_lines(command, "OK", timeout_ms, 1);
}

int at_command_at(void){
    ESP_LOGI(TAG, "running AT");
    return at_command_with_ok_ack("AT", 600);
}

int at_command_echo_set(bool on){
    if(on){
        return at_command_with_ok_ack("ATE1", 300);
    }
    // if echo is already enabled, we can expect a line with our cmd on the reply
    return at_command_with_ack_and_lines("ATE0", "OK", 500, 2);
}

static int at_command_two_line_response(char* command, char * result_buff, int buff_len,
                                       int first_timeout_ms, int second_timeout_ms){
    char at_buffer[LINE_BUFFER_SIZE] = {0};

    ESP_LOGD(TAG, "Sending {%s}2", command);
    send_line(command);

    int name_result = await_line(at_buffer, pdMS_TO_TICKS(first_timeout_ms));
    if(name_result != pdPASS){
        return -3;
    }

    ESP_LOGD(TAG, "copying %s for max %d", at_buffer, buff_len);
    strncpy(result_buff, at_buffer, buff_len);

    int result = await_line(at_buffer, pdMS_TO_TICKS(second_timeout_ms));
    if(result != pdPASS){
        return -4;
    }

    if(!strstr(at_buffer, "OK")){
        return -1;
    }
    return 0;
}

int at_command_get_model_name(char *name, int buff_len){
    return at_command_two_line_response("AT+CGMM", name, buff_len, 300, 300);
}

int at_command_get_imei(char *imei, int buff_len){
    return at_command_two_line_response("AT+CGSN", imei, buff_len, 300, 300);
}

int at_command_get_ccid(char *ccid, int buff_len){
    return at_command_two_line_response("AT+QCCID", ccid, buff_len, 300, 300);
}

int at_command_get_imsi(char *imsi, int buff_len){
    return at_command_two_line_response("AT+CIMI", imsi, buff_len, 300, 300);
}

int at_command_get_operator(char *operator, int buff_len){
    return at_command_two_line_response("AT+COPS?", operator, buff_len, 300, 300);
}

int at_command_pdp_define(void){
    return at_command_with_ok_ack("AT+CGDCONT=1,\"IP\",\"mdatks\"", 400);
}


int at_command_dial(void){
    return at_command_with_ack_and_lines("ATD*99***1#", "CONNECT", 300, 1);
}

int at_command_data_mode(void){
    return at_command_with_ack_and_lines("ATO", "CONNECT 150000000", 300, 1);
}

int at_command_signal_strength(char *sysmode, int *rssi, int *rsrp, int *sinr, int *rsrq){
    char buffer[LINE_BUFFER_SIZE];
    int comms_result = at_command_two_line_response("AT+QCSQ", buffer, LINE_BUFFER_SIZE, 300, 300);
    if(comms_result ==0){
        ESP_LOGI(TAG, "Parsing signal strength: \"%s\"", buffer);
        // result example: +QCSQ: "eMTC",-65,-85,168,-5

        char *result_head = strtok(buffer, "\"");
        char *sysmode_start = strtok(NULL, "\"");
        if((result_head!=NULL) &&(sysmode_start!=NULL)){
            strncpy(sysmode, sysmode_start, 10);
            char *numbers_string = strtok(NULL, "\"");
            ESP_LOGI(TAG, "splitting numbers [%s]", numbers_string);

            int matches = sscanf(numbers_string, ",%d,%d,%d,%d", rssi, rsrp, sinr, rsrq);
            if(matches==4){
                ESP_LOGI(TAG, "matches %d. values: %d, %d, %d, %d", matches, *rssi, *rsrp, *sinr, *rsrq);
                return 0;
            }
            ESP_LOGE(TAG, "Failed to match all values for signal strength [%d fund]", matches);
            return -3;

        }else{
            ESP_LOGE(TAG, "failed parsing of signal strength response %p and %p", result_head, sysmode_start);
            return -2;
        }
    }
    ESP_LOGE(TAG, "failed signal strength command");
    return -1;

}

int at_command_signal_quality(int *rssi, int *ber){
    char buffer[LINE_BUFFER_SIZE];
    int comms_result = at_command_two_line_response("AT+CSQ", buffer, LINE_BUFFER_SIZE, 300, 300);
    if(comms_result ==0){
        ESP_LOGI(TAG, "Parsing signal quality: \"%s\"", buffer);
        // result example: +CSQ: 28,99

        int matches = sscanf(buffer, "+CSQ: %d,%d", rssi, ber);
            if(matches==2){
                ESP_LOGI(TAG, "matches %d. values: %d, %d ", matches, *rssi, *ber);
                return 0;
            }
            ESP_LOGW(TAG, "Parsing failed");
            return -3;
        
    }
    ESP_LOGE(TAG, "failed signal strength command");
    return -1;

}

int at_command_flow_ctrl_enable(void){
    return at_command_with_ok_ack("AT+IFC=2,2", 600);
}

int at_command_network_registration_status(){
    char reply[20];
    return at_command_two_line_response("AT+CREG?", reply, 20, 300, 100);
}

int at_command_set_baud_high(void){
    return at_command_with_ok_ack("AT+IPR=921600;&W", 300);
}

int at_command_set_baud_low(void){
    return at_command_with_ok_ack("AT+IPR=115200;&W", 300);
}

int at_command_detect_echo(void){
    char at_buffer[LINE_BUFFER_SIZE] = {0};
    char *command = "AT";
    int timeout = 300;
    

    ESP_LOGD(TAG, "Sending [%s]", command);
    send_line(command);

    int first_line_result = await_line(at_buffer, pdMS_TO_TICKS(timeout));

    if(first_line_result != pdPASS){
        ESP_LOGW(TAG, "At command got no reply");
        return -1;
    }

    if(strstr(at_buffer, command)){
        ESP_LOGD(TAG, "echo detected, waiting for ack");
        int second_line_result = await_line(at_buffer, pdMS_TO_TICKS(timeout));
        if(second_line_result!=pdPASS){
            ESP_LOGW(TAG, "No ack after echo");
            return -2;
        }

        if(strstr(at_buffer, "OK")){
            ESP_LOGI(TAG, "got echo and ack");
            return 2;
        }

        ESP_LOGW(TAG, "failed to get ack after echo");
        return -3;

    }else if(strstr(at_buffer, "OK")){
        ESP_LOGI(TAG, "detected only ack -> echo off");
        return 1;
    }

    ESP_LOGE(TAG, "unknown response '%s'", at_buffer);
    clear_lines();
    return -4;

}

int at_command_activate_pdp_context(void){
    // note the long response time
    return at_command_with_ok_ack("AT+QIACT=1", 150*1000);
}


int at_command_deactivate_pdp_context(void){
    // note the long response time
    return at_command_with_ok_ack("AT+QIDEACT=1", 40*1000);
}


int at_command_ping_test(int *sent,int *rcvd,int *lost, int *min, int *max, int *avg){
    char at_buffer[LINE_BUFFER_SIZE] = {0};
    int timeout_ms = 5*1000;

    char * command = "AT+QPING=1,\"8.8.8.8\",10,4";
    ESP_LOGD(TAG, "Sending %s", command);
    send_line(command);

    int ok_result = await_line(at_buffer, pdMS_TO_TICKS(timeout_ms));
    if(ok_result != pdPASS){
        return -1;
    }

    if(!strstr(at_buffer, "OK")){
        return -2;
    }


    bool error = false;
    for(int i=0; i<4; i++){
        int line_result = await_line(at_buffer, pdMS_TO_TICKS(timeout_ms));
        if(line_result != pdPASS){
            error = true;
            //continue the loop to make sure to read all lines returned by the modem
        }

        if(!strstr(at_buffer, "+QPING: 0")){
            ESP_LOGE(TAG, "ping line error");
            error = true;
        }
    }

    if(error){
        return -5;
    }

    int final_result = await_line(at_buffer, pdMS_TO_TICKS(timeout_ms));
    if(final_result != pdPASS){
        return -3;
    }

    if(!strstr(at_buffer, "+QPING: 0")){
        return -4;
    }

    int matches = sscanf(at_buffer, "+QPING: 0,%d,%d,%d,%d,%d,%d]",
                         sent, rcvd, lost, min, max, avg
                  );

    if(matches==6){
        ESP_LOGI(TAG, "avg ping %d", *avg);
        return 0;
    }

    ESP_LOGW(TAG, "Parsing failed, matches: %d", matches);
    return -5;

}
