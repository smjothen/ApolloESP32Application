#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

#define NO_DATA_TIMEOUT_SEC 10

static const char *TAG = "WEBSOCKET";

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

static void ocpp_task(void *pvParameters)
{
    ESP_LOGI(TAG, "staring ocpp ws client");
    esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();
    websocket_cfg.uri = "ws://echo.websocket.org";

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
    char data[32];
    int i = 0;
    while (i < 10) {
        if (esp_websocket_client_is_connected(client)) {
            int len = sprintf(data, "hello %04d", i++);
            ESP_LOGI(TAG, "Sending %s", data);
            esp_websocket_client_send(client, data, len, portMAX_DELAY);
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    esp_websocket_client_stop(client);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);

    while (true)
    {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    
}

void ocpp_task_start(void)
{
    // esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
    // esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "staring ocpp ws client soon");
    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096;
    xTaskCreate( ocpp_task, "ocppTask", stack_size, &ucParameterToPass, 5, &taskHandle );
}
