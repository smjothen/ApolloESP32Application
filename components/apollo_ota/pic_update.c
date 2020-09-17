#include "esp_log.h"
#include <string.h>

#include "pic_update.h"

#include "zaptec_protocol_serialisation.h"
#include "protocol_task.h"
#include "mcu_communication.h"
#include "crc32.h"


#define TAG "pic_update"


extern const uint8_t dspic_bin_start[] asm("_binary_dspic_bin_start");
extern const uint8_t dspic_bin_end[] asm("_binary_dspic_bin_end");

#define _DSPIC_LINE_SIZE 128*4
const uint32_t DSPIC_LINE_SIZE = _DSPIC_LINE_SIZE;
#define DSPIC_APP_START 0x3C00

ZapMessage txMsg;
// ZEncodeMessageHeader* does not check the length of the buffer!
// This should not be a problem for most usages, but make sure strings are within a range that fits!
uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];


#define COMMAND_NACK         0x00
#define COMMAND_ACK          0x01
#define COMMAND_READ_PM      0x02
#define COMMAND_WRITE_PM     0x03
#define COMMAND_WRITE_CM     0x07
#define COMMAND_START_APP    0x08
#define COMMAND_START_BL     0x18
#define COMMAND_READ_ID      0x09
#define COMMAND_APP_CRC      0x0A
#define COMMAND_APP_DELETE   0x0B
#define COMMAND_WRITE_HEADER 0x0C


int transfer_dspic_fw(void);
int boot_dspic_app(void);
int delete_dspic_fw(void);
int set_dspic_header(void);

void update_dspic_task(void){
    
    if(delete_dspic_fw()>=0){
        ESP_LOGI(TAG, "update stage delete: success!");
    }else{
        goto err_delete;
    }

    if(transfer_dspic_fw()>=0){
        ESP_LOGI(TAG, "update stage transfer: success!");
    }else{
        goto err_flash;
    }

    if(set_dspic_header()>=0){
        ESP_LOGI(TAG, "update stage header: success!");
    }else{
        goto err_header;
    }

    if(boot_dspic_app()>=0){
        ESP_LOGI(TAG, "update stage boot: success!");
    }else{
        goto err_app_boot;
    }

    ESP_LOGI(TAG, "SUCCESS, dspic updated");
    return;

    err_delete:
        ESP_LOGW(TAG, "failed to delete dspic fw");
    err_flash:
        ESP_LOGW(TAG, "failed to flash dspic fw");
    err_header:
        ESP_LOGW(TAG, "failed to flash dspic header");
    err_app_boot:
        ESP_LOGW(TAG, "failed to boot new fw");
}

int update_dspic(void){
    update_dspic_task();

    return 0;
}

int transfer_dspic_fw(void){
    ESP_LOGI(TAG, "sending fw to dspic");

    int32_t fw_byte_size = dspic_bin_end - dspic_bin_start;

    if(fw_byte_size%DSPIC_LINE_SIZE!=0){
        ESP_LOGE(TAG, "firmware for dsPIC not aligned to row size, fw_byte_size %d", fw_byte_size);
    }

    int32_t fw_line_count = fw_byte_size / DSPIC_LINE_SIZE;
    ESP_LOGI(TAG, "will flash %u lines, %u bytes; each line is %d bytes", fw_line_count, fw_byte_size, DSPIC_LINE_SIZE);
    //fw_line_count = 1;

    int address_size = 4;
    uint8_t message_data[1 + address_size +DSPIC_LINE_SIZE ];
        
    txMsg.type = MsgFirmware;
    txMsg.identifier = ParamRunTest; // ignored by bootloader

    for(int line=0; line<fw_line_count; line++){
        
        uint32_t words_per_line = DSPIC_LINE_SIZE/2;
        uint32_t address = DSPIC_APP_START + (line*words_per_line);
        const uint8_t *start_of_line = dspic_bin_start + (line*DSPIC_LINE_SIZE);

        message_data[0] = COMMAND_WRITE_PM;
        memcpy(message_data+1, &address, address_size);
        memcpy(message_data+1+address_size, start_of_line, DSPIC_LINE_SIZE);

        uint encoded_length = ZEncodeMessageHeaderAndByteArray(
            &txMsg, (char *) message_data, sizeof(message_data), txBuf, encodedTxBuf
        );

        ESP_LOGI(TAG, "sending fw line %d for addr %d message, %d bytes, %d lines to go",line, address, encoded_length, fw_line_count-line);

        // printf(">>data to uart: \n\r\t");
        // int i;
        // for(i=0; i<sizeof(message_data);i++){
        //     printf("%2x   ", message_data[i]);
        //     if((i+1)%16==0) printf("\n\r\t");
        //     else if ((i+1)%8 ==0) printf("\t\t");            
        // }
        
        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
        printf("frame type: %d \n\r", rxMsg.type);
        printf("frame identifier: %d \n\r", rxMsg.identifier);
        printf("frame timeId: %d \n\r", rxMsg.timeId);

        uint8_t message_type = rxMsg.type;
        uint8_t error_code = ZDecodeUInt8(rxMsg.data);

        freeZapMessageReply();

        if((message_type != MsgFirmwareAck) || (error_code != 0)){
            ESP_LOGW(TAG, "error in dspic response when writing FW (error: %d)", error_code);
            return -1;
        }

        
    }

    return 0;
}

int boot_dspic_app(void){
    ESP_LOGI(TAG, "starting dsPIC app");

    txMsg.type = MsgFirmware;
    txMsg.identifier = ParamRunTest;

    uint encoded_length = ZEncodeMessageHeaderAndOneByte(
        &txMsg, COMMAND_START_APP, txBuf, encodedTxBuf
    );

    ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
    
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
    printf("frame type: %d \n\r", rxMsg.type);
    printf("frame identifier: %d \n\r", rxMsg.identifier);
    printf("frame timeId: %d \n\r", rxMsg.timeId);

    uint8_t message_type = rxMsg.type;
    uint8_t error_code = ZDecodeUInt8(rxMsg.data);

    freeZapMessageReply();

    if((message_type != MsgFirmwareAck) || (error_code != 0)){
        ESP_LOGW(TAG, "error in dspic response when giving boot command (error: %d)", error_code);
        return -1;
    }

    return 0;
}

int delete_dspic_fw(void){
    ESP_LOGI(TAG, "deleting dsPIC FW");

    ESP_LOGI(TAG, "creating zap message");
    
    txMsg.type = MsgFirmware;
    txMsg.identifier = ParamRunTest; // ignored on bootloader?

    uint encoded_length = ZEncodeMessageHeaderAndOneByte(
        &txMsg, COMMAND_APP_DELETE, txBuf, encodedTxBuf
    );

    ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
    
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    uint8_t message_type = rxMsg.type;
    uint8_t error_code = ZDecodeUInt8(rxMsg.data);

    freeZapMessageReply();

    if((message_type != MsgFirmwareAck) || (error_code != 0)){
        ESP_LOGW(TAG, "error in dspic response when deleteing app (error: %d)", error_code);
        return -1;
    }

    return 0;
    
}

int set_dspic_header(void){
    ESP_LOGI(TAG, "sending header to dsPIC");


    ZapMessage txMsg;

    // ZEncodeMessageHeader* does not check the length of the buffer!
    // This should not be a problem for most usages, but make sure strings are within a range that fits!
    uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
    uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
    
    txMsg.type = MsgFirmware;
    txMsg.identifier = ParamRunTest; // ignored on bootloader?

    uint8_t message_data[10];
    message_data[0] = COMMAND_WRITE_HEADER;
    message_data[1] = 1; // header version

    uint32_t app_size = dspic_bin_end - dspic_bin_start; // this must be a multiple of 4

    uint32_t app_crc = crc32(0,(const char *) dspic_bin_start, app_size);

    memcpy(message_data+2, &app_size, sizeof(uint32_t));
    memcpy(message_data+2+4, &app_crc, sizeof(uint32_t));

    ESP_LOGI(TAG, "Sending crc %u for %u bytes", app_crc, app_size);

    uint encoded_length = ZEncodeMessageHeaderAndByteArray(
        &txMsg, (char *) message_data, sizeof(message_data), txBuf, encodedTxBuf
    );

    ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
    
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
    uint8_t message_type = rxMsg.type;
    uint8_t error_code = ZDecodeUInt8(rxMsg.data);

    freeZapMessageReply();

    if((message_type != MsgFirmwareAck) || (error_code != 0)){
        ESP_LOGW(TAG, "error in dspic response when writing header (error: %d)", error_code);
        return -1;
    }else{
        ESP_LOGI(TAG, "dsPIC flashed, and header updated");
    }

    return 0;

}
