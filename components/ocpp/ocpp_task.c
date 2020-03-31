#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "cJSON.h"
#include "iot_button.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_system.h"
#include "time.h"

#include "ocpp_task.h"
#include "ocpp_call.h"

#define NO_DATA_TIMEOUT_SEC 10

static const char *TAG = "WEBSOCKET";
#define OCPP_MESSAGE_MAX_LENGTH 4096 // TODO: Check with standard

esp_websocket_client_handle_t client;

SemaphoreHandle_t ocpp_request_pending;
cJSON *request_reply;
char pending_request_unique_id[25];

QueueHandle_t ocpp_response_recv_queue;
QueueHandle_t ocpp_send_queue;

void updateRequestUniqueId(void){
    int result = sprintf(
        pending_request_unique_id,
        "apollo-%08x-%08x",
        esp_random(), esp_random()
    );
    configASSERT(result==24);
    ESP_LOGI(TAG, "pending_request_unique_id: %s", pending_request_unique_id);
}

struct tm parseTime(const char * string){
    // the date time in a json schema is compliant with RFC 5.6 https://json-schema.org/understanding-json-schema/reference/string.html
    // e.g. "1985-04-12T23:20:50.52Z", "1990-12-31T15:59:60-08:00"
    // parseing based on https://stackoverflow.com/questions/26895428/how-do-i-parse-an-iso-8601-date-with-optional-milliseconds-to-a-struct-tm-in-c

    int y,M,d,h,m;
    float s;
    int tzh = 0, tzm = 0;

    int paresed_arguments = sscanf(string, "%d-%d-%dT%d:%d:%f%d:%dZ", &y, &M, &d, &h, &m, &s, &tzh, &tzm);

    if (6 < paresed_arguments) {
        if (tzh < 0) {
        tzm = -tzm;    // Fix the sign on minutes.
        }
    }

    struct tm time;
    time.tm_year = y - 1900; // Year since 1900
    time.tm_mon = M - 1;     // 0-11
    time.tm_mday = d;        // 1-31
    time.tm_hour = h;        // 0-23
    time.tm_min = m;         // 0-59
    time.tm_sec = (int)s;    // 0-61 (0-60 in C++11)

    //TODO: return tz and maybe handle sub-sec resolution

    return time;
}

cJSON *runCall(const char* action, cJSON *payload){

    if( xSemaphoreTake( ocpp_request_pending, portMAX_DELAY ) == pdTRUE )
    {
        xQueueReset(ocpp_response_recv_queue);
        updateRequestUniqueId();

        cJSON *call = cJSON_CreateArray();
        cJSON_AddItemToArray(call, cJSON_CreateNumber(2)); //[<MessageTypeId>, is call
        cJSON_AddItemToArray(call, cJSON_CreateString(pending_request_unique_id));
        cJSON_AddItemToArray(call, cJSON_CreateString(action));
        cJSON_AddItemToArray(call, payload);
        char *request_string = cJSON_Print(call);
        if (request_string == NULL)
        {
            ESP_LOGE(TAG, "cJSON_Print failed for ocpp hb");
            configASSERT(false);
        }

        configASSERT(strlen(request_string)<OCPP_MESSAGE_MAX_LENGTH);
        cJSON_Delete(call); // Free BOTH the newly created and the payload passed to the routine

        esp_websocket_client_send(client, request_string, strlen(request_string), portMAX_DELAY);
        free(request_string);

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

void replyToCall(cJSON *message){
    char *unique_id = cJSON_GetArrayItem(message, 1)->valuestring;
    char *action = cJSON_GetArrayItem(message, 2)->valuestring;
    cJSON *payload = cJSON_GetArrayItem(message, 3);

    cJSON *reply_payload = cJSON_CreateObject();

    ESP_LOGI(TAG, "Handleing call %s", action);

    if(strcmp(action, "ChangeAvailability") == 0){
        ESP_LOGI(
            TAG, "Changing avaiability to %s for connector %d",
            cJSON_GetObjectItem(payload, "type")->valuestring,
            cJSON_GetObjectItem(payload, "connectorId")->valueint
        );

        cJSON_AddStringToObject(reply_payload, "status", "Accepted");
    }

    cJSON *callReply = cJSON_CreateArray();
    cJSON_AddItemToArray(callReply, cJSON_CreateNumber(3)); //[<MessageTypeId>, is callresult
    cJSON_AddItemToArray(callReply, cJSON_CreateString(unique_id));
    cJSON_AddItemToArray(callReply, reply_payload);
   
    char *reply_string = cJSON_Print(callReply);
    configASSERT(strlen(reply_string)<OCPP_MESSAGE_MAX_LENGTH);
    cJSON_Delete(callReply);
    esp_websocket_client_send(client, reply_string, strlen(reply_string), portMAX_DELAY);
    free(reply_string);
    ESP_LOGI(TAG, "sent callreply");
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
            ESP_LOGI(TAG, "central system sent call:%s", message_string);
            replyToCall(message);
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

        switch (data->op_code){
        case 1:
            //ws text frame
            ocpp_web_ws_event_handler(data); 
            break;
        case 9:
        case 10:
            // ws layer ping pong
            break;
        default:
            ESP_LOGE(TAG, "unhandled websocket op code");
            configASSERT(false);
            break;
        }

        break;
        
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

void on_btn(void* arg){
    cJSON *auth_call_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(auth_call_payload, "idTag", "12345678901234567888");
    cJSON *authorize_response = runCall("Authorize", auth_call_payload);
    freeOcppReply(authorize_response);
}

void send_heartbeat(TimerHandle_t xTimer){
    ESP_LOGI(TAG, "sending ocpp heartbeat ");
    cJSON *auth_call_payload = cJSON_CreateObject();
    cJSON *authorize_response = runCall("Heartbeat", auth_call_payload);

    char *central_system_time = cJSON_GetObjectItem(
        cJSON_GetArrayItem(authorize_response, 2),
        "currentTime"
    )->valuestring;
    ESP_LOGI(TAG, "got ocpp hb reply<------------[servertime:%s]",
    central_system_time);

    struct tm calendar_time = parseTime(central_system_time);
    time_t posix_time = mktime(&calendar_time);
    ESP_LOGI(TAG, "parsed time: %s", ctime(&posix_time));

    freeOcppReply(authorize_response);
}

static void ocpp_task(void *pvParameters)
{
    ocpp_request_pending = xSemaphoreCreateMutex();

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
    int heartbeat_interval = cJSON_GetObjectItem(
        cJSON_GetArrayItem( on_boot_response, 2),
         "interval"
    )->valueint;
    freeOcppReply(on_boot_response);

    if(heartbeat_interval<=0){
        heartbeat_interval = 10;
    }

    ESP_LOGI(TAG, "staring heartbeat timer at %d sec period", heartbeat_interval);
    TimerHandle_t timer = xTimerCreate( 
            "Ocpp-HB-timer",
            ( 1000 * heartbeat_interval ) / portTICK_RATE_MS,
            pdTRUE,
            NULL,
            send_heartbeat
    );
    xTimerStart(timer, 0);

    ESP_LOGI(TAG, "_/^\\_/^\\_/^\\_/^\\_/^\\");
    
    vTaskDelay(1000 / portTICK_RATE_MS);

    while (true)
    {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    
}

void ocpp_task_start(void)
{
    ESP_LOGI(TAG, "staring ocpp ws client soon");

    #define BUTTON_IO_NUM           0
    #define BUTTON_ACTIVE_LEVEL     0
    button_handle_t btn_handle = iot_button_create(BUTTON_IO_NUM, BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, on_btn, "RELEASE");
    }

    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096 + (2*OCPP_MESSAGE_MAX_LENGTH);
    xTaskCreate( ocpp_task, "ocppTask", stack_size, &ucParameterToPass, 5, &taskHandle );
}
