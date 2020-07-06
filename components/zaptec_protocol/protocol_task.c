#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/xtensa_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"
#include "mcu_communication.h"

#define TAG __FILE__

#define RX_TIMEOUT  (3000 / (portTICK_PERIOD_MS))

void uartCommsTask(void *pvParameters);
void uartRecvTask(void *pvParameters);
void configureUart();
void onCharRx(char c);

SemaphoreHandle_t uart_write_lock;
QueueHandle_t uart_recv_message_queue;
QueueHandle_t uart0_events_queue;

const int uart_num = UART_NUM_2;

void zaptecProtocolStart(){
    ESP_LOGI(TAG, "starting protocol task");
    static uint8_t ucParameterToPass = {0};
    TaskHandle_t uartRecvTaskHandle = NULL;
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096;
    xTaskCreate( uartRecvTask, "uartRecvTask", stack_size, &ucParameterToPass, 5, &uartRecvTaskHandle );
    xTaskCreate( uartCommsTask, "UARTCommsTask", stack_size, &ucParameterToPass, 5, &taskHandle );
    configASSERT(uartRecvTaskHandle);
    configASSERT( taskHandle );
    if( taskHandle == NULL ){
        ESP_LOGE(TAG, "failed to start task");
    }
    
}

ZapMessage runRequest(const uint8_t *encodedTxBuf, uint length){

    if( xSemaphoreTake( uart_write_lock, RX_TIMEOUT ) == pdTRUE )
    {
    
        uart_flush(uart_num);
        xQueueReset(uart_recv_message_queue);

        uart_write_bytes(uart_num, (char *)encodedTxBuf, length);

        ZapMessage rxMsg;
        xQueueReceive( 
            uart_recv_message_queue,
            &( rxMsg ),
            portMAX_DELAY
        );

        // dont release uart_write_lock, let caller use freeZapMessageReply()
        return rxMsg;
    }
    configASSERT(false);
    ZapMessage dummmy_reply = {0};
    return dummmy_reply;
}

void freeZapMessageReply(){
    xSemaphoreGive(uart_write_lock) ;
}

void uartRecvTask(void *pvParameters){
    uart_write_lock = xSemaphoreCreateMutex();
    configASSERT(uart_write_lock);
    uart_recv_message_queue = xQueueCreate( 1, sizeof( ZapMessage ) );
    configASSERT(uart_recv_message_queue);

    configureUart();

    ZapMessage rxMsg;
    uint8_t uart_data_size = 128;
    uint8_t uart_data[uart_data_size];

    uart_event_t event;

    while(true)
        {
            //configASSERT(xQueueReceive(uart0_events_queue, (void * )&event, (portTickType)RX_TIMEOUT))

            //if(event.type != UART_DATA){continue;}

//            if(uxSemaphoreGetCount(uart_write_lock)==1){
//                ESP_LOGE(TAG, "got uart data without outstanding request");
//                continue;
//            }

            //configASSERT(event.size <= uart_data_size);
            //int length = uart_read_bytes(uart_num, uart_data, event.size, RX_TIMEOUT);
    	int length = uart_read_bytes(uart_num, uart_data, 1, RX_TIMEOUT);

            ESP_LOGI(TAG, "feeding %d bytes to ZParseFrame:", length);

            for(int i = 0; i<length; i++){
                uint8_t rxByte = uart_data[i];

                if(ZParseFrame(rxByte, &rxMsg))
                {   
                    uart_flush(uart_num);
                    configASSERT(xQueueSend(
                        uart_recv_message_queue,                        
                        ( void * ) &rxMsg,
                        portMAX_DELAY
                    ))
                    printf("handling frame\n\r");
                }
            }
        }
}

void uartCommsTask(void *pvParameters){
    ESP_LOGI(TAG, "configuring uart");

    while (true)
    {
        // tx test
        ESP_LOGI(TAG, "creating zap message");
        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        
        /*txMsg.type = MsgRead;//MsgWrite;
        txMsg.identifier = ParamInternalTemperature;//ParamRunTest;
        uint encoded_length = ZEncodeMessageHeader(
        		&txMsg, txBuf
		);*/

        txMsg.identifier = ParamRunTest;
        txMsg.type = MsgWrite;
        uint encoded_length = ZEncodeMessageHeaderAndOneByte(
            &txMsg, 34, txBuf, encodedTxBuf
        );

        ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
        
        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
        printf("frame type: %d \n\r", rxMsg.type);
        printf("frame identifier: %d \n\r", rxMsg.identifier);
        printf("frame timeId: %d \n\r", rxMsg.timeId);

        uint8_t error_code = ZDecodeUInt8(rxMsg.data);
        printf("frame error code: %d\n\r", error_code);
        freeZapMessageReply();

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
}

void configureUart(){
    int tx_pin = GPIO_NUM_26;
    int rx_pin = GPIO_NUM_25;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    const int uart_buffer_size = (1024 * 2);

    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, \
        uart_buffer_size, 20, &uart0_events_queue, 0));

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));    

    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}
