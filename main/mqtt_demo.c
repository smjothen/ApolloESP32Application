#include "mqtt_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "cJSON.h"

#include "mqtt_demo.h"

#define BROKER_URL "mqtt://mqtt.eclipse.org"
static const char *TAG = "mqtt_demo";
int mqtt_count = 0;

char *payloadstring;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/esp-pppos", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", "[esp test Norway2]", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        ESP_LOGD(TAG, "publishing %s", payloadstring);
        if(mqtt_count<3){
            msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", payloadstring, 0, 0, 0);
            mqtt_count += 1;
        }else
            // xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }
    return ESP_OK;
}

esp_mqtt_client_handle_t mqtt_client;

void start_mqtt_demo(void){
    vTaskDelay(pdMS_TO_TICKS(2500));
    cJSON *json_payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(json_payload, "test_val", 42.0);
    payloadstring = cJSON_PrintUnformatted(json_payload);

    esp_mqtt_client_config_t mqtt_config = {
        .uri = BROKER_URL,
        .event_handle = mqtt_event_handler,
    };
    //esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_config);
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_LOGI(TAG, "starting mqtt");
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_disconnect()
{
	esp_mqtt_client_disconnect(mqtt_client);
    ESP_LOGI(TAG, "mqtt disconnecting");
}

void mqtt_reconnect()
{
	esp_mqtt_client_reconnect(mqtt_client);
    ESP_LOGI(TAG, "mqtt reconnecting");
}
