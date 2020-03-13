#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"

#define TAG __FILE__

void uartCommsTask(void *pvParameters);
void configureUart();
void onCharRx(char c);

const int uart_num = UART_NUM_2;

void zaptecProtocolStart(){
    ESP_LOGI(TAG, "starting protocol task");
    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096;
    xTaskCreate( uartCommsTask, "UARTCommsTask", stack_size, &ucParameterToPass, 5, &taskHandle );
    configASSERT( taskHandle );
    if( taskHandle == NULL ){
        ESP_LOGE(TAG, "failed to start task");
    }
    
}

void uartCommsTask(void *pvParameters){
    ESP_LOGI(TAG, "configuring uart");
    configureUart();

    while (true)
    {
        // tx test
        ESP_LOGI(TAG, "creating zap message");
        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        
        txMsg.type = MsgWrite;
        txMsg.identifier = ParamRunTest;

        uint encoded_length = ZEncodeMessageHeaderAndOneByte(
            &txMsg, 34, txBuf, encodedTxBuf
        );

        ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);

        uart_flush(uart_num);
        uart_write_bytes(uart_num, (char *)encodedTxBuf, encoded_length);

        ZapMessage rxMsg;
        bool reply_pending = true;
        uint32_t empty_uart_count = 0;
        while(reply_pending)
        {
            if(empty_uart_count>15){
                ESP_LOGW(TAG, "missed reply from dsPIC");
                break;
            }

            uint8_t uart_data[128];
            int length = uart_read_bytes(uart_num, uart_data, 128, 0);

            if(length == 0){
                ESP_LOGI(TAG, "got no reply bytes");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                empty_uart_count++;
                continue;
            }else{
                empty_uart_count = 0;
            }

            ESP_LOGI(TAG, "feeding %d bytes to ZParseFrame:", length);

            for(int i = 0; i<length; i++){
                uint8_t rxByte = uart_data[i];
                // ESP_LOGI(TAG, "eating byte: %d", rxByte);

                if(ZParseFrame(rxByte, &rxMsg))
                {   
                    uart_flush(uart_num);
                    reply_pending = false;
                    printf("handling frame\n\r");
                    printf("frame type: %d \n\r", rxMsg.type);
                    printf("frame identifier: %d \n\r", rxMsg.identifier);
                    printf("frame timeId: %d \n\r", rxMsg.timeId);

                    uint8_t error_code = ZDecodeUInt8(rxMsg.data);
                    printf("frame error code: %d\n\r", error_code);
                }
            
            }
            
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    
}

void configureUart(){
    int tx_pin = GPIO_NUM_17;
    int rx_pin = GPIO_NUM_16;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    const int uart_buffer_size = (1024 * 2);
    QueueHandle_t uart_queue;
    // Install UART driver using an event queue here"
    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, \
        uart_buffer_size, 10, &uart_queue, 0));

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));    

    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void onCharRx(char c){
    //ESP_LOGI(TAG, "got char: %c", c);
}