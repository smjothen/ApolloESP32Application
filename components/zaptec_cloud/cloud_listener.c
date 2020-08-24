#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "zaptec_cloud_listener.h"
#include "sas_token.h"
#include "zaptec_cloud_observations.h"

#include "esp_transport_ssl.h"
//#include "mqtt_outbox.h"
//#include "mqtt_supported_features.h"

//#include "../components/mqtt/esp-mqtt/lib/include/mqtt_msg.h"
#include "../lib/include/mqtt_msg.h"

#define TAG "Cloud Listener"

#define MQTT_HOST "zapcloud.azure-devices.net"
//const char device_id[15];
//const char device_id[] = "ZAP000005";
//const char device_id[] = "ZAP000007";
//const char device_id[] = "ZAP000008";
//#define DEVICE_ID device_id
#define ROUTING_ID "default"
#define INSTALLATION_ID "a0d00d05-b959-4466-9a22-13271f0e0c0d"
#define MQTT_PORT 8883

#define MQTT_USERNAME_PATTERN "%s/%s/?api-version=2018-06-30"
#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8&ri=default&ii=a0d00d05-b959-4466-9a22-13271f0e0c0d"
//#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8"
/*#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8"
(existing != null ? existing + "&" : "")
				+ "$.ct=application%2Fjson&$.ce=utf-8"
				+ (addRouting ?
				   (routingId != null && routingId.Length > 0 ? "&ri=" + Uri.EscapeDataString(routingId) : "")
					+ (encodedInstallationId != null ? "&ii=" + encodedInstallationId : "")
				   : "");*/

bool doNewAck = false;

int resetCounter = 0;

const char event_topic[128];
const char event_topic_hold[128];

static struct DeviceInfo cloudDeviceInfo;

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
esp_mqtt_client_config_t mqtt_config = {0};
char token[256];  // token was seen to be at least 136 char long

int refresh_token(esp_mqtt_client_config_t *mqtt_config){
    create_sas_token(1*60, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
	//create_sas_token(1*3600, &token);
    ESP_LOGE(TAG, "connection token is %s", token);
    mqtt_config->password = token;
    return 0;
}

int publish_iothub_event(const char *payload){
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


int publish_iothub_ack(const char *payload){

	//if(doNewAck == false)
		return 0;

    if(mqtt_client == NULL){
        return -1;
    }

    //mqtt_msg_puback(&mqtt_client->mqtt_state.mqtt_connection, msg_id);

   /* int message_id = esp_mqtt_client_publish(
            mqtt_client, event_topic_hold,
            payload, 0, 0, 0
    );*/

    doNewAck = false;

//    if(message_id>0){
//        return 0;
//    }
    return -2;
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    mqtt_client = event->client;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        char devicebound_topic[128];
        sprintf(devicebound_topic, "devices/{%s}/messages/devicebound/#", cloudDeviceInfo.serialNumber);

        esp_mqtt_client_subscribe(mqtt_client, "$iothub/methods/POST/#", 1);
        esp_mqtt_client_subscribe(mqtt_client, devicebound_topic, 1);
        esp_mqtt_client_subscribe(mqtt_client, "$iothub/twin/res/#", 1);

        publish_debug_message_event("mqtt connected", cloud_event_level_information);

        // request twin data
//        esp_mqtt_client_publish(
//            mqtt_client, "$iothub/twin/GET/?$rid=1",
//            NULL, 0, 1, 0
//        );

        resetCounter = 0;

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
        printf("rTOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("rDATA=%.*s\r\n", event->data_len, event->data);



        if(strstr(event->topic, "iothub/twin/res/200/"))
        {

        	//TOPIC=$iothub/twin/res/200/?$rid=
        	//DATA={"desired":{"Settings":{"120":"1","520":"1","711":"1","802":"Apollo05"},"$version":4},"reported":{"$version":1}}

        	//publish_debug_telemetry_observation_cloud_settings();
        	//esp_mqtt_client_publish(mqtt_client, event->topic, NULL, 0, 0, 0);
        	//ESP_LOGD(TAG, "RESPONDED?");
        }

        if(strstr(event->topic, "iothub/methods/POST/300/"))
        {
        	//memcpy(event_topic_hold, event->topic, 128);
        	//strcpy(event_topic_hold,"$iothub/methods/res/?$rid=1");
        	//doNewAck = true;
        	//publish_iothub_ack("", event->topic);

        	//esp_mqtt_client_publish(event->client, event->topic, "", 0, 0, 0);
        	//publish_debug_telemetry_observation_local_settings();
        }


        if(strstr(event->topic, "iothub/methods/POST/102/"))
        {

        }

        // ESP_LOGD(TAG, "publishing %s", payloadstring);
        // if(mqtt_count<3){
        //     msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", payloadstring, 0, 0, 0);
        //     mqtt_count += 1;
        // }else
        //     // xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "About to connect, refreshing the token");
        refresh_token(&mqtt_config);
        esp_mqtt_set_config(mqtt_client, &mqtt_config);
        break;
    case MQTT_EVENT_ERROR:
    	resetCounter++;

    	ESP_LOGI(TAG, "MQTT_EVENT_ERROR: %d/10", resetCounter);

        if(resetCounter == 10)
        {
        	esp_restart();
        }

        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }
    return ESP_OK;
}


void start_cloud_listener_task(struct DeviceInfo deviceInfo){

	cloudDeviceInfo = deviceInfo;

	ESP_LOGI(TAG, "Connecting to IotHub");

    static char broker_url[128] = {0};
    sprintf(broker_url, "mqtts://%s", MQTT_HOST);

    static char username[128];
    sprintf(username, "%s/%s/?api-version=2018-06-30", MQTT_HOST, cloudDeviceInfo.serialNumber);

    sprintf(
        event_topic, MQTT_EVENT_PATTERN,
		cloudDeviceInfo.serialNumber
    );

    ESP_LOGI(TAG,
        "mqtt connection:\r\n"
        " > uri: %s\r\n"
        " > port: %d\r\n"
        " > username: %s\r\n"
        " > client id: %s\r\n"
        " > cert_pem len: %d\r\n",
        //" > cert_pem: %s\r\n",
        broker_url, MQTT_PORT, username, cloudDeviceInfo.serialNumber,
        strlen(cert)//, cert
    );

    mqtt_config.uri = broker_url;
    mqtt_config.event_handle = mqtt_event_handler;
    mqtt_config.port = MQTT_PORT;
    mqtt_config.username = username;
    mqtt_config.client_id = cloudDeviceInfo.serialNumber;
    mqtt_config.cert_pem = cert;

    mqtt_config.lwt_qos = 1;
    mqtt_config.lwt_topic = event_topic;
    static char *lwt = "{\"EventType\":30,\"Message\":\"mqtt connection broke[lwt]\",\"Type\":5}";
    mqtt_config.lwt_msg = lwt;

    mqtt_config.disable_auto_reconnect = false;
    mqtt_config.reconnect_timeout_ms = 10000;

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_LOGI(TAG, "starting mqtt");
    esp_mqtt_client_start(mqtt_client);
}
