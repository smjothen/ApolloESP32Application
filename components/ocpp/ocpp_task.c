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
#define OCPP_MESSAGE_MAX_LENGTH 4096 // TODO: Check with standard

esp_websocket_client_handle_t client;

SemaphoreHandle_t ocpp_request_pending;
cJSON *request_reply;
uint32_t pending_request_unique_id;

QueueHandle_t ocpp_response_recv_queue;
QueueHandle_t ocpp_send_queue;

cJSON *runCall(const char* action, cJSON *payload){

    if( xSemaphoreTake( ocpp_request_pending, portMAX_DELAY ) == pdTRUE )
    {
        xQueueReset(ocpp_response_recv_queue);
        pending_request_unique_id++;

        cJSON *call = cJSON_CreateArray();
        cJSON_AddItemToArray(call, cJSON_CreateNumber(2)); //[<MessageTypeId>, is call
        cJSON_AddItemToArray(call, cJSON_CreateNumber(pending_request_unique_id));
        cJSON_AddItemToArray(call, cJSON_CreateString(action));
        cJSON_AddItemToArray(call, payload);
        char *request_string = cJSON_Print(call);
        configASSERT(strlen(request_string)<OCPP_MESSAGE_MAX_LENGTH);
        cJSON_Delete(call); // Free BOTH the newly created and the payload passed to the routine

        esp_websocket_client_send(client, request_string, strlen(request_string), portMAX_DELAY);

        xQueueReceive( 
            ocpp_response_recv_queue,
            &request_reply,
            portMAX_DELAY
        );

        // dont release ocpp_request_pending, let caller use freeOcppReply()
        ESP_LOGI(TAG, "--->got reply type %d", cJSON_GetArrayItem(request_reply, 0)->valueint);
        return request_reply;
    }
    configASSERT(false);
    cJSON *dummmy_reply = {0};
    return dummmy_reply;
}

void freeOcppReply(){
    cJSON_Delete(request_reply);
    xSemaphoreGive(ocpp_request_pending) ;
}

void ocpp_web_ws_event_handler(esp_websocket_event_data_t *data){
    // cJSON requires a nullterminated string
        char message_string[OCPP_MESSAGE_MAX_LENGTH] = {0};

        configASSERT(data->data_len<OCPP_MESSAGE_MAX_LENGTH-1);
        memcpy(message_string, data->data_ptr, data->data_len);
        cJSON *message = cJSON_Parse(message_string);
        // the message is freed here if it is a call from the server
        // for results and call errors a pointer is passed to a queue
        // and the consumer will/ MUST free it

        int message_type_id = cJSON_GetArrayItem(message, 0)->valueint;
        
        switch (message_type_id)
        {
        case 2:
            // central system sent call
            ESP_LOGI(TAG, "central system sent call");
            // TODO: handle call
            cJSON_Delete(message);
            break;
        case 3:
            // central system sent CallResult
            ESP_LOGI(TAG, "central system sent result");
            configASSERT(xQueueSend(ocpp_response_recv_queue, (void * )&message, (portTickType)portMAX_DELAY))
            break;

        case 4:
            // central system sent callerror
            configASSERT(xQueueSend(ocpp_response_recv_queue, (void * )&message, (portTickType)portMAX_DELAY))
            ESP_LOGE(TAG, "central system sent callerror");

        
            break;
        default:
            ESP_LOGE(TAG, "unknown message type id from occp cs: %d", message_type_id);
        }
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

        if(data->op_code == 9 || data->op_code == 10){
            // ws layer ping pong
        } else{
            ocpp_web_ws_event_handler(data);    
        }
        break;
        
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

static void ocpp_task(void *pvParameters)
{
    ocpp_request_pending = xSemaphoreCreateMutex();
    pending_request_unique_id = 0;

    ocpp_response_recv_queue = xQueueCreate( 1, sizeof(cJSON *) );
    ocpp_send_queue = xQueueCreate( 1, sizeof(char)*OCPP_MESSAGE_MAX_LENGTH );
    
    ESP_LOGI(TAG, "staring ocpp ws client");
    esp_websocket_client_config_t websocket_cfg = {};

    websocket_cfg.uri = "ws://192.168.10.174:9000/testcp001";
    websocket_cfg.subprotocol = "ocpp1.6";

    int default_ws_stack_size = 4*1024;
    websocket_cfg.task_stack = default_ws_stack_size + (4 * OCPP_MESSAGE_MAX_LENGTH);

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    client = esp_websocket_client_init(&websocket_cfg);
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

    cJSON *on_boot_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(on_boot_payload, "chargePointVendor", "VendorX");
    cJSON_AddStringToObject(on_boot_payload, "chargePointModel", "SingleSocketCharger");
    cJSON *on_boot_response = runCall("BootNotification", on_boot_payload);
    freeOcppReply(on_boot_response);
    vTaskDelay(1000 / portTICK_RATE_MS);

    cJSON *auth_call_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(auth_call_payload, "idTag", "12345678901234567888");
    cJSON *authorize_response = runCall("Authorize", auth_call_payload);
    freeOcppReply(authorize_response);
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
    int stack_size = 4096 + (2*OCPP_MESSAGE_MAX_LENGTH);
    xTaskCreate( ocpp_task, "ocppTask", stack_size, &ucParameterToPass, 5, &taskHandle );
}
