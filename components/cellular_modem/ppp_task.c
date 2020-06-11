#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h> 

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "ppp_task.h"

static const char *TAG = "PPP_TASK";

#define GPIO_OUTPUT_PWRKEY		21
#define GPIO_OUTPUT_RESET		33
#define GPIO_OUTPUT_DEBUG_LED    0

#define CELLULAR_RX_SIZE 256
#define CELLULAR_TX_SIZE 256
#define CELLULAR_QUEUE_SIZE 30
#define ECHO_TEST_TXD1  (GPIO_NUM_17)
#define ECHO_TEST_RXD1  (GPIO_NUM_16)
#define ECHO_TEST_RTS1  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS1  (UART_PIN_NO_CHANGE)
#define RD_BUF_SIZE 256

#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET)

static QueueHandle_t uart_queue;

void hard_reset_cellular(void){
    gpio_config_t io_conf; 
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);

    ESP_LOGI(TAG, "BG reset start");
	gpio_set_level(GPIO_OUTPUT_RESET, 1);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_RESET, 0);
	vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
	vTaskDelay(200 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
	vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "BG reset done");
}

static void configure_uart(void){
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD1, ECHO_TEST_RXD1, ECHO_TEST_RTS1, ECHO_TEST_CTS1);
    uart_driver_install(
        UART_NUM_1, CELLULAR_RX_SIZE, CELLULAR_TX_SIZE,
        CELLULAR_QUEUE_SIZE, &uart_queue, 0
    );

}

static void on_uart_data(uint8_t* event_data,size_t size){
    event_data[size] = 0;
    ESP_LOGI(TAG, "got uart data[%s]", event_data);
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            memset(dtmp, 0, RD_BUF_SIZE);
            ESP_LOGI(TAG, "uart[%d] event:", UART_NUM_1);
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(UART_NUM_1, dtmp, event.size, portMAX_DELAY);
                    on_uart_data(dtmp, event.size);
                    // uart_write_bytes(UART_NUM_1, (const char*) dtmp, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void ppp_task_start(void){
    ESP_LOGI(TAG, "Configuring BG9x");
    hard_reset_cellular();
    configure_uart();
    ESP_LOGI(TAG, "uart configured");
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
}