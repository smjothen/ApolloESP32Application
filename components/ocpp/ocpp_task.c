#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "cJSON.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

#define NO_DATA_TIMEOUT_SEC 10

static const char *TAG = "WEBSOCKET";


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

    websocket_cfg.uri = "ws://192.168.10.174:9000/testcp001";
    websocket_cfg.subprotocol = "ocpp1.6";

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
    int i = 0;
    while (i < 10) {
        if (esp_websocket_client_is_connected(client)) {
            break;
        }
        i++;
        ESP_LOGI(TAG, "waiting for connection");
        vTaskDelay(500 / portTICK_RATE_MS);
    }
    
    cJSON *on_boot_call = cJSON_CreateArray();
    cJSON_AddItemToArray(on_boot_call, cJSON_CreateNumber(2));
    cJSON_AddItemToArray(on_boot_call, cJSON_CreateNumber(19223201));
    cJSON_AddItemToArray(on_boot_call, cJSON_CreateString("BootNotification"));
    cJSON *on_boot_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(on_boot_payload, "chargePointVendor", "VendorX");
    cJSON_AddStringToObject(on_boot_payload, "chargePointModel", "SingleSocketCharger");
    cJSON_AddItemToArray(on_boot_call, on_boot_payload);
    char *on_boot_call_string = cJSON_Print(on_boot_call);
    
    esp_websocket_client_send(client, on_boot_call_string, strlen(on_boot_call_string), portMAX_DELAY);
    vTaskDelay(1000 / portTICK_RATE_MS);

    cJSON *auth_call = cJSON_CreateArray();
    cJSON_AddItemToArray(auth_call, cJSON_CreateNumber(2));
    cJSON_AddItemToArray(auth_call, cJSON_CreateNumber(19223202));
    cJSON_AddItemToArray(auth_call, cJSON_CreateString("Authorize"));
    cJSON *auth_call_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(auth_call_payload, "idTag", "12345678901234567899");
    cJSON_AddItemToArray(auth_call, auth_call_payload);
    char *auth_call_string = cJSON_Print(auth_call);
    esp_websocket_client_send(client, auth_call_string, strlen(auth_call_string), portMAX_DELAY);
    vTaskDelay(1000 / portTICK_RATE_MS);

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
