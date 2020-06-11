#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "ppp_task.h"

static const char *TAG = "PPP_TASK";

#define GPIO_OUTPUT_PWRKEY		21
#define GPIO_OUTPUT_RESET		33
#define GPIO_OUTPUT_DEBUG_LED    0

#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET)

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

void ppp_task_start(void){
    ESP_LOGI(TAG, "Resetting BG96");
}