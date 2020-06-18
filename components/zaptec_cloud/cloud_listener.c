#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "zaptec_cloud_listener.h"
#include "sas_token.h"

#define TAG "Cloud Listener"

#define MQTT_HOST "zapcloud.azure-devices.net"
#define DEVICE_ID "ZAP000001"
#define ROUTING_ID "myri"
#define INSTALLATION_ID "myii"
#define MQTT_PORT 8883

#define MQTT_USERNAME_PATTERN "%s/%s/?api-version=2018-06-30"
//#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8&ri=%s&ii=%s"
#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8"
const char event_topic[128];

const char cert[] =
"-----BEGIN CERTIFICATE-----\r\n"
"MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ\r\n"
"RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD\r\n"
"VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTAwMDUxMjE4NDYwMFoX\r\n"
"DTI1MDUxMjIzNTkwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y\r\n"
"ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy\r\n"
"VHJ1c3QgUm9vdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr\r\n"
"mD1X6CZymrV51Cni4eiVgLGw41uOKymaZN+hXe2wCQVt2yguzmKiYv60iNoS6zjr\r\n"
"IZ3AQSsBUnuId9Mcj8e6uYi1agnnc+gRQKfRzMpijS3ljwumUNKoUMMo6vWrJYeK\r\n"
"mpYcqWe4PwzV9/lSEy/CG9VwcPCPwBLKBsua4dnKM3p31vjsufFoREJIE9LAwqSu\r\n"
"XmD+tqYF/LTdB1kC1FkYmGP1pWPgkAx9XbIGevOF6uvUA65ehD5f/xXtabz5OTZy\r\n"
"dc93Uk3zyZAsuT3lySNTPx8kmCFcB5kpvcY67Oduhjprl3RjM71oGDHweI12v/ye\r\n"
"jl0qhqdNkNwnGjkCAwEAAaNFMEMwHQYDVR0OBBYEFOWdWTCCR1jMrPoIVDaGezq1\r\n"
"BE3wMBIGA1UdEwEB/wQIMAYBAf8CAQMwDgYDVR0PAQH/BAQDAgEGMA0GCSqGSIb3\r\n"
"DQEBBQUAA4IBAQCFDF2O5G9RaEIFoN27TyclhAO992T9Ldcw46QQF+vaKSm2eT92\r\n"
"9hkTI7gQCvlYpNRhcL0EYWoSihfVCr3FvDB81ukMJY2GQE/szKN+OMY3EU/t3Wgx\r\n"
"jkzSswF07r51XgdIGn9w/xZchMB5hbgF/X++ZRGjD8ACtPhSNzkE1akxehi/oCr0\r\n"
"Epn3o0WC4zxe9Z2etciefC7IpJ5OCBRLbf1wbWsaY71k5h+3zvDyny67G7fyUIhz\r\n"
"ksLi4xaNmjICq44Y3ekQEe5+NauQrz4wlHrQMz2nZQ/1/I6eYs9HRCwBXbsdtTLS\r\n"
"R9I4LtD+gdwyah617jzV/OeBHRnDJELqYzmp\r\n"
"-----END CERTIFICATE-----\r\n"
;

esp_mqtt_client_handle_t mqtt_client = {0};

int publish_iothub_event(char *payload){
    if(mqtt_client == NULL){
        return -1;
    }

    int message_id = esp_mqtt_client_publish(
            mqtt_client, event_topic,
            payload, 0, 1, 0
    );

    if(message_id>0){
        return 0;
    }
    return -2;
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    mqtt_client = event->client;
    int msg_id;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        time_t now = 0;
        struct tm timeinfo = { 0 };
        char strftime_buf[64];
        time(&now);
        setenv("TZ", "UTC-0", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);

        char demo_payload [256];
        sprintf(demo_payload, "{\"Type\":1,\"ObservationId\":808,\"ObservedAt\":\"%s\",\"Value\":\"mqtt connected\"}", strftime_buf);
        ESP_LOGI(TAG, "sending \"%s\" on topic [%s] with timestamp %s",
            demo_payload, event_topic, strftime_buf
        );

        //[msg_id = esp_mqtt_client_subscribe(client, "/topic/esp-pppos", 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        publish_iothub_event(demo_payload);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", "[esp test Norway2]", 0, 0, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
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
        // ESP_LOGD(TAG, "publishing %s", payloadstring);
        // if(mqtt_count<3){
        //     msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", payloadstring, 0, 0, 0);
        //     mqtt_count += 1;
        // }else
        //     // xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "About to connect, could we refresh the token here?");
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




void start_cloud_listener_task(void){
    ESP_LOGI(TAG, "Connecting to IotHub");

    char token[256];  // token was seen to be at least 136 char long
    create_sas_token(60*60, &token);
    ESP_LOGE(TAG, "connection token is %s", token);

    char broker_url[128] = {0};
    sprintf(broker_url, "mqtts://%s", MQTT_HOST);

    char username[128];
    sprintf(username, "%s/%s/?api-version=2018-06-30", MQTT_HOST, DEVICE_ID);

    sprintf(
        event_topic, MQTT_EVENT_PATTERN,
        DEVICE_ID
    );

    ESP_LOGI(TAG,
        "mqtt connection:\r\n"
        " > uri: %s\r\n"
        " > port: %d\r\n"
        " > username: %s\r\n"
        " > client id: %s\r\n"
        " > token: %s\r\n"
        " > token len: %d\r\n"
        " > cert_pem len: %d\r\n"
        " > cert_pem: %s\r\n",
        broker_url, MQTT_PORT, username, DEVICE_ID,
        token, strlen(token), strlen(cert), cert
    );

    esp_mqtt_client_config_t mqtt_config = {
        .uri = broker_url,
        .event_handle = mqtt_event_handler,
        .port = MQTT_PORT,
        .username = username,
        .client_id = DEVICE_ID,
        .password = token,
        .cert_pem = cert
    };
    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_LOGI(TAG, "starting mqtt");
    esp_mqtt_client_start(mqtt_client);
}