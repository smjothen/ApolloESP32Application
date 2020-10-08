#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "zaptec_cloud_listener.h"
#include "sas_token.h"
#include "zaptec_cloud_observations.h"

#include "esp_transport_ssl.h"
#include "../zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../zaptec_protocol/include/protocol_task.h"

#include "../lib/include/mqtt_msg.h"
#include "../../main/storage.h"
#include "../i2c/include/i2cDevices.h"

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

bool mqttConnected = false;

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

bool isMqttConnected()
{
	return mqttConnected;
}

esp_mqtt_client_handle_t mqtt_client = {0};
esp_mqtt_client_config_t mqtt_config = {0};
char token[256];  // token was seen to be at least 136 char long

int refresh_token(esp_mqtt_client_config_t *mqtt_config){
    //create_sas_token(1*60, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
	create_sas_token(3600, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
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


void ParseCloudSettingsFromCloud(char * message, int message_len)
{
	if ((message[0] != '{') || (message[message_len-1] != '}'))
		return;

	char recvString[message_len];
	strncpy(recvString, message+1, message_len-2);
	recvString[message_len-2] = '\0';

	//Replace apostrophe with space for sscanf() to work
	for (int i = 0; i < message_len; i++)
	{
		if(recvString[i] == '"')
			recvString[i] = ' ';
	}

	char * messageStart = strchr(recvString,'{');

	char const separator[2] = ",";
	char * stringPart;

	stringPart = strtok(recvString, separator);

	bool doSave = false;

	while(stringPart != NULL)
	{
		char * pos = strstr(stringPart, " 120 : ");
		if(pos != NULL)
		{

			int useAuthorization = 0;
			sscanf(pos+strlen(" 120 : "),"%d", &useAuthorization);
			ESP_LOGI(TAG, "120 useAuthorization: %u \n", useAuthorization);
			storage_Set_AuthenticationRequired((uint8_t)useAuthorization);
			doSave = true;
		}


		pos = strstr(stringPart, " 711 : ");
		if(pos != NULL)
		{

			int isEnabled = 0;
			sscanf(pos+strlen(" 711 : "),"%d", &isEnabled);
			ESP_LOGI(TAG, "711 isEnabled: %d \n", isEnabled);
			storage_Set_IsEnabled((uint8_t)isEnabled);
			doSave = true;
		}

		pos = strstr(stringPart, " 802 : ");
		if(pos != NULL)
		{

			char chargerName[33] = {0};
			sscanf(pos+strlen(" 802 : "),"%32s", chargerName);//Read Max 32 characters
			ESP_LOGI(TAG, "802 chargerName: %s \n", chargerName);
			storage_Set_ChargerName(chargerName);
			doSave = true;
		}

		//			if(strstr(stringPart, "standalone_setting") != NULL)
		//			{
		//				stringPart = strtok(NULL, separator);
		//			}


		stringPart = strtok(NULL, separator);
		ESP_LOGI(TAG, "Str: %s \n", stringPart);

	}
        	//rTOPIC=$iothub/twin/PATCH/properties/desired/?$version=15
        	//rDATA={"Settings":{"120":"0","711":"1","802":"Apollo14","511":"10","520":"1","805":"0","510":"20"},"$version":15}

			//rTOPIC=$iothub/twin/PATCH/properties/desired/?$version=3
			//rDATA={"Settings":{"802":"Apollo16","711":"1","120":"1","520":"1"},"$version":3}

	if(doSave == true)
	{
		esp_err_t err = storage_SaveConfiguration();
		ESP_LOGI(TAG, "Saved CloudSettings: %s=%d\n", (err == 0 ? "OK" : "FAIL"), err);
	}
	else
	{
		ESP_LOGI(TAG, "CloudSettings: Nothing to save");
	}

}

void ParseLocalSettingsFromCloud(char * message, int message_len)
{
	if ((message[0] != '[') || (message[message_len-1] != ']'))
		return;

	char recvString[message_len];
	strncpy(recvString, message+1, message_len-2);
	recvString[message_len-2] = '\0';

	char const separator[2] = ",";
	char * stringPart;

	stringPart = strtok(recvString, separator);

	if(stringPart != NULL)
	{
		ESP_LOGI(TAG, "Str: %s \n", stringPart);
		if(strstr(stringPart, "Device_Parameters") != NULL)
		{
			stringPart = strtok(NULL, separator);
			ESP_LOGI(TAG, "Str: %s \n", stringPart);

			if(strstr(stringPart, "standalone_setting") != NULL)
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';

				if(strstr(stringPart, "system"))
				{
					storage_Set_Standalone(0);
					esp_err_t err = storage_SaveConfiguration();
					ESP_LOGI(TAG, "Saved SYSTEM, %s=%d\n", (err == 0 ? "OK" : "FAIL"), err);

				}
				else if(strstr(stringPart, "standalone"))
				{
					storage_Set_Standalone(0);
					esp_err_t err = storage_SaveConfiguration();
					ESP_LOGI(TAG, "Saved STANDALONE, %s=%d\n", (err == 0 ? "OK" : "FAIL"), err);
				}
			}

			else if(strstr(stringPart, "standalone_phase") != NULL)
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				uint8_t standalonePhase = atoi(stringPart+1);

				//Allow only 4 settings: TN_L1=1, TN_L3=4, IT_L1_L3=IT_1P=8, IT_L1_L2_L3=IT_3P=9
				if((standalonePhase == 1) || (standalonePhase == 4) || (standalonePhase == 8) || (standalonePhase == 9))
				{
					storage_Set_StandalonePhase(standalonePhase);
					esp_err_t err = storage_SaveConfiguration();
					ESP_LOGI(TAG, "Saved STANDALONE_PHASE=%d, %s=%d\n", standalonePhase, (err == 0 ? "OK" : "FAIL"), err);
				}
				else
				{
					ESP_LOGI(TAG, "Invalid standalonePhase: %d \n", standalonePhase);
				}
			}

			else if(strstr(stringPart, "max_standalone_current"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				float maxStandaloneCurrent = atof(stringPart+1);

				storage_Set_StandaloneCurrent(maxStandaloneCurrent);
				esp_err_t err = storage_SaveConfiguration();
				ESP_LOGI(TAG, "Saved STANDALONE_CURRENT=%f, %s=%d\n", maxStandaloneCurrent, (err == 0 ? "OK" : "FAIL"), err);


				//MCU_SendParameter(ParamHmiBrightness, &hmiBrightness, sizeof(float));
				//MCU_SendParameter(ParamHmiBrightness, hmiBrightness);
			}


			else if(strstr(stringPart, "network_type"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				int networkType = 0;
				if(strstr(stringPart, "IT_1"))
					networkType = 1;
				else if(strstr(stringPart, "IT_3"))
					networkType = 2;
				else if(strstr(stringPart, "TN_1"))
					networkType = 3;
				else if(strstr(stringPart, "TN_3"))
					networkType = 4;

				if(networkType != 0)
				{
					storage_Set_NetworkType(networkType);
					esp_err_t err = storage_SaveConfiguration();
					ESP_LOGI(TAG, "Saved NETWORK TYPE=%d, %s=%d\n", networkType, (err == 0 ? "OK" : "FAIL"), err);
				}
				else
				{
					ESP_LOGI(TAG, "Invalid NetworkType: %d \n", networkType);
				}

				ESP_LOGI(TAG, "Network type: %d\n", networkType);
			}

			else if(strstr(stringPart, "hmi_brightness"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				volatile float hmiBrightness = atof(stringPart+1);
				//ESP_LOGI(TAG, "Float: %f \n", hmiBrightness);

				storage_Set_StandaloneCurrent(hmiBrightness);
				esp_err_t err = storage_SaveConfiguration();
				ESP_LOGI(TAG, "Saved HMI_BRIGHTNESS=%f, %s=%d\n", hmiBrightness, (err == 0 ? "OK" : "FAIL"), err);

				//MCU_SendParameter(ParamHmiBrightness, &hmiBrightness, sizeof(float));
				//MCU_SendParameter(ParamHmiBrightness, hmiBrightness);
			}
		}
		else if(strstr(stringPart, "Cable"))
		{
			stringPart = strtok(NULL, separator);

			if(strstr(stringPart, "permanent_lock"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';

				uint8_t lockValue = 0;
				if(strstr(stringPart,"true") || strstr(stringPart,"True"))
				{
					lockValue = 1;
				}
				else if(strstr(stringPart,"false") || strstr(stringPart,"False"))
				{
					lockValue = 0;
				}

				storage_Set_PermanentLock(lockValue);
				esp_err_t err = storage_SaveConfiguration();
				ESP_LOGI(TAG, "Saved PermanentLock=%d, %s=%d\n", lockValue, (err == 0 ? "OK" : "FAIL"), err);

			}


			//MCU_SendParameter(ParamHmiBrightness, &hmiBrightness, sizeof(float));
			//MCU_SendParameter(ParamHmiBrightness, hmiBrightness);
		}

	}

}

static bool restartCmdReceived = false;

void cloud_listener_check_cmd()
{
	if(restartCmdReceived == true)
	{
		vTaskDelay(pdMS_TO_TICKS(3000));
		esp_restart();
	}
}


int ParseCommandFromCloud(esp_mqtt_event_handle_t commandEvent)
{
	int responseStatus = 0;

	//Don't spend time in this function, must return from mqtt-event. May need separate process
	if(strstr(commandEvent->topic, "iothub/methods/POST/102/"))
	{
		ESP_LOGI(TAG, "Received \"Restart ESP32\"-command");
		//Execute delayed in another thread to allow command ack to be sent to cloud
		restartCmdReceived = true;
		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/103/"))
	{
		ESP_LOGI(TAG, "Received \"Restart MCU\"-command");
		ESP_LOGI(TAG, "TODO: Implement");
		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/200/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmware\"-command");
		ESP_LOGI(TAG, "TODO: Implement");
		responseStatus = 400;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/200/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmwareForced\"-command");
		ESP_LOGI(TAG, "TODO: Implement");
		responseStatus = 400;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/501/"))
	{
		//rDATA=["16","4"]
		char commandString[commandEvent->data_len+1];
		//char commandString[20] = {0};
		commandString[commandEvent->data_len] = '\0';
		strncpy(commandString, commandEvent->data, commandEvent->data_len);

		//Replace apostrophe with space for sscanf() to work
		for (int i = 0; i < commandEvent->data_len; i++)
		{
			if(commandString[i] == '"')
				commandString[i] = ' ';
		}

		float currentFromCloud = 0;
		int phaseFromCloud = 0;
		sscanf(commandString,"%*s%f%*s%d%*s", &currentFromCloud, &phaseFromCloud);

		ESP_LOGI(TAG, "Start: %f PhaseId: %d \n", currentFromCloud, phaseFromCloud);
		responseStatus = 200;
	}


	return responseStatus;
}


static void BuildLocalSettingsResponse(char * responseBuffer)
{
	//char * data = "\"[Device_Parameters]\\nserial = ZAP000014\\nmid = ZAP000014\\ncommunication_mode = Wifi\\nstandalone_setting = standalone\\nmax_standalone_current = 16.00\\nnetwork_type = TN_3\\nstandalone_phase = 4\\nhmi_brightness = 0.4\\n\\n[Wifi_Parameters]\\nname = xxx\\npassword = <masked>\\n\\n[BLE_Parameters]\\nconnect-pin = 0000\\n\\n[Cable]\\npermanent_lock = False\\n\\n\"";
	char * data = "\"[Device_Parameters]\\nserial = ZAP000014\\nmid = ZAP000014\\n"
			"communication_mode = Wifi\\n"
			"standalone_setting = standalone\\n"
			"max_standalone_current = 16.00\\n"
			"network_type = TN_3\\nstandalone_phase = 4\\nhmi_brightness = 0.4\\n\\n[Wifi_Parameters]\\nname = xxx\\npassword = <masked>\\n\\n[BLE_Parameters]\\nconnect-pin = 0000\\n\\n[Cable]\\npermanent_lock = False\\n\\n\"";

	sprintf(responseBuffer, "\"[Device_Parameters]\\nserial = %s\\nmid = %s\\n", i2cGetLoadedDeviceInfo().serialNumber, i2cGetLoadedDeviceInfo().serialNumber);

	if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
		sprintf(responseBuffer+strlen(responseBuffer), "communication_mode = Wifi\\n");
	else if(storage_Get_CommunicationMode() == eCONNECTION_LTE)
		sprintf(responseBuffer+strlen(responseBuffer), "communication_mode = LTE\\n");

	if(storage_Get_Standalone() == 0)
		sprintf(responseBuffer+strlen(responseBuffer), "standalone_setting = system\\n");
	else if(storage_Get_Standalone() == 1)
		sprintf(responseBuffer+strlen(responseBuffer), "standalone_setting = standalone\\n");

	sprintf(responseBuffer+strlen(responseBuffer), "max_standalone_current = %f\\n", storage_Get_StandaloneCurrent());

	if(storage_Get_NetworkType() == 1)
		sprintf(responseBuffer+strlen(responseBuffer), "network_type = IT_1\\n");
	else if(storage_Get_NetworkType() == 2)
		sprintf(responseBuffer+strlen(responseBuffer), "network_type = IT_3\\n");
	else if(storage_Get_NetworkType() == 3)
		sprintf(responseBuffer+strlen(responseBuffer), "network_type = TN_1\\n");
	else if(storage_Get_NetworkType() == 4)
		sprintf(responseBuffer+strlen(responseBuffer), "network_type = TN_3\\n");

	sprintf(responseBuffer+strlen(responseBuffer), "standalone_phase = %d\\n", storage_Get_StandalonePhase());
	sprintf(responseBuffer+strlen(responseBuffer), "hmi_brightness = %f\\n\n", storage_Get_HmiBrightness());

	sprintf(responseBuffer+strlen(responseBuffer), "[Wifi_Parameters]\\nname =  %s\\npassword = <masked>\\n\n", " ");

	sprintf(responseBuffer+strlen(responseBuffer), "[BLE_Parameters]\\nconnect-pin = %s\\n\n", i2cGetLoadedDeviceInfo().Pin);

	if(storage_Get_PermanentLock() == 1)
		sprintf(responseBuffer+strlen(responseBuffer), "[Cable]\\npermanent_lock = true\\n\n\"");
	else
		sprintf(responseBuffer+strlen(responseBuffer), "[Cable]\\npermanent_lock = false\\n\n\"");

}



static int ridNr = 4199;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    mqtt_client = event->client;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        //char devicebound_topic[128];
        //sprintf(devicebound_topic, "devices/{%s}/messages/devicebound/#", cloudDeviceInfo.serialNumber);
        //sprintf(devicebound_topic, "devices/%s/messages/devicebound/#", cloudDeviceInfo.serialNumber);

        // Cloud-to-device
        esp_mqtt_client_subscribe(mqtt_client, "$iothub/methods/POST/#", 2);
        //esp_mqtt_client_subscribe(mqtt_client, devicebound_topic, 2);

        // Device twin
        esp_mqtt_client_subscribe(mqtt_client, "$iothub/twin/res/#", 2);
        esp_mqtt_client_subscribe(mqtt_client, "$iothub/twin/PATCH/properties/desired/#", 2);


        publish_debug_message_event("mqtt connected", cloud_event_level_information);

        // request twin data
        ridNr++;
        char devicetwin_topic[64];
        sprintf(devicetwin_topic, "$iothub/twin/GET/?$rid=%d", ridNr);
        esp_mqtt_client_publish(mqtt_client, devicetwin_topic, NULL, 0, 1, 0);

        resetCounter = 0;

        mqttConnected = true;

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqttConnected = false;
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

        if(strstr(event->topic, "iothub/twin/PATCH/properties/desired/"))
		{
        	ParseCloudSettingsFromCloud(event->data, event->data_len);
        	//rTOPIC=$iothub/twin/PATCH/properties/desired/?$version=15
        	//rDATA={"Settings":{"120":"0","711":"1","802":"Apollo14","511":"10","520":"1","805":"0","510":"20"},"$version":15}

		}
        if(strstr(event->topic, "iothub/twin/res/200/"))
        {

        	//char devicetwin_topic[64];

			//volatile char ridString[event->topic_len];

			//strncpy(ridString, event->topic, event->topic_len);
			//volatile char * ridSubString = strstr(ridString, "$rid=");
			//char *strPart;
			//volatile int rid = (int)strtol(ridSubString+5, &strPart, 10);
			//sprintf(devicetwin_topic, "$iothub/twin/res/200/?rid=%d", ridNr);//ridSubString);
			//esp_mqtt_client_publish(mqtt_client, devicetwin_topic, NULL, 0, 1, 0);

        	//TOPIC=$iothub/twin/res/200/?$rid=
        	//DATA={"desired":{"Settings":{"120":"1","520":"1","711":"1","802":"Apollo05"},"$version":4},"reported":{"$version":1}}

        	//publish_debug_telemetry_observation_cloud_settings();
        	//esp_mqtt_client_publish(mqtt_client, event->topic, NULL, 0, 0, 0);
        	//ESP_LOGD(TAG, "RESPONDED?");
        }

        if(strstr(event->topic, "iothub/methods/POST/300/"))
        {

        	if(event->data_len > 10)
        	{
        		ParseLocalSettingsFromCloud(event->data, event->data_len);
        	}

        	//Build LocalSettings-response
			char devicetwin_topic[64];
			volatile char ridString[event->topic_len+1];
			strncpy(ridString, event->topic, event->topic_len);
			ridString[event->topic_len] = '\0';
			volatile char * ridSubString = strstr(ridString, "$rid=");

			//volatile int rid = (int)strtol(ridSubString+5, &strPart, 10);
			sprintf(devicetwin_topic, "$iothub/methods/res/200/?%s", ridSubString);
			//char * data = "\"[Device_Parameters]\\nserial = ZAP000014\\nmid = ZAP000014\\ncommunication_mode = Wifi\\nstandalone_setting = standalone\\nmax_standalone_current = 16.00\\nnetwork_type = TN_3\\nstandalone_phase = 4\\nhmi_brightness = 0.4\\n\\n[Wifi_Parameters]\\nname = xxx\\npassword = <masked>\\n\\n[BLE_Parameters]\\nconnect-pin = 0000\\n\\n[Cable]\\npermanent_lock = False\\n\\n\"";
			//esp_mqtt_client_publish(mqtt_client, devicetwin_topic, data, 0, 1, 0);

			char responseBuffer[500]={0};
			BuildLocalSettingsResponse(responseBuffer);
			ESP_LOGD(TAG, "responseStringLength: %d", strlen(responseBuffer));

			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, responseBuffer, 0, 1, 0);


        	//esp_mqtt_client_publish(event->client, event->topic, "", 0, 0, 0);
        	//publish_debug_telemetry_observation_local_settings();
        }

        //Handle incoming commands
        if(strstr(event->topic, "iothub/methods/POST/"))
        {
        	int responseStatus = ParseCommandFromCloud(event);

    		char devicetwin_topic[64];

    		volatile char ridString[event->topic_len+1];

    		strncpy(ridString, event->topic, event->topic_len);
    		ridString[event->topic_len] = '\0';
    		volatile char * ridSubString = strstr(ridString, "$rid=");

    		sprintf(devicetwin_topic, "$iothub/methods/res/%d/?%s", responseStatus, ridSubString);//200 = OK, 400 = FAIL

    		char * data = NULL;
    		esp_mqtt_client_publish(mqtt_client, devicetwin_topic, data, 0, 1, 0);

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

        if(resetCounter == 5)
        {
        	esp_err_t rconErr = esp_mqtt_client_reconnect(mqtt_client);
        	ESP_LOGI(TAG, "MQTT event reconnect! Error: %d", rconErr);
        }

        if(resetCounter == 10)
        {
        	ESP_LOGI(TAG, "MQTT_EVENT_ERROR restart");
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
    mqtt_config.keepalive = 120;
    //mqtt_config.refresh_connection_after_ms = 30000;

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_LOGI(TAG, "starting mqtt");
    esp_mqtt_client_start(mqtt_client);
}

void stop_cloud_listener_task()
{
	esp_mqtt_client_stop(mqtt_client);
}
