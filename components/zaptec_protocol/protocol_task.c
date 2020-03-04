#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "protocol_task.h"

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
        char* test_str = "test\n";
        uart_write_bytes(uart_num, (const char*)test_str, strlen(test_str));

        // rx test
        uint8_t data[128];
        int length = 0;
        //ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));

        length = uart_read_bytes(uart_num, data, 128, 10);
        ESP_LOGI(TAG, "passing %d bytes from system drivers's uart buffer", length);
        for(int i=0; i<length; i++){
            onCharRx((char) data[i]);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
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