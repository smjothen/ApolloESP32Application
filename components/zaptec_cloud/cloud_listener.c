#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "zaptec_cloud_listener.h"
#include "sas_token.h"
#include "zaptec_cloud_observations.h"

#include "esp_transport_ssl.h"
#include "../zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../zaptec_protocol/include/protocol_task.h"

#include "../lib/include/mqtt_msg.h"
#include "../../main/storage.h"
#include "../i2c/include/i2cDevices.h"
#include "../i2c/include/RTC.h"
#include "../authentication/authentication.h"
#include "../../main/chargeSession.h"
#include "../../main/sessionHandler.h"
#include "apollo_ota.h"
#include "ble_interface.h"
#include "../cellular_modem/include/ppp_task.h"
#include "../wifi/include/network.h"
#include "../../main/certificate.h"

#include "esp_tls.h"

#define TAG "Cloud Listener"

#ifdef DEVELOPEMENT_URL
	#define MQTT_HOST "zap-d-iothub.azure-devices.net" //FOR DEVELOPEMENT
#else
	#define MQTT_HOST "zapcloud.azure-devices.net"
#endif

#define MQTT_PORT 8883

#define MQTT_USERNAME_PATTERN "%s/%s/?api-version=2018-06-30"
#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8&ri=%s&ii=%s"

int resetCounter = 0;

char event_topic[128];
static struct DeviceInfo cloudDeviceInfo;

static bool mqttConnected = false;
static bool mqttSimulateOffline = false;
bool cloudSettingsAreUpdated = false;
bool localSettingsAreUpdated = false;
bool reportGridTestResults = false;
bool MCUDiagnosticsResults = false;
bool ESPDiagnosticsResults = false;
bool reportInstallationConfigOnFile = false;
bool simulateTlsError = false;




/*const char cert[] =
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
;*/


void MqttSetDisconnected()
{
	mqttConnected = false;
}

void MqttSetSimulatedOffline(bool simOffline)
{
	mqttSimulateOffline = simOffline;
}

bool isMqttConnected()
{
	if(mqttSimulateOffline == true)
		return false;
	else
		return mqttConnected;
}

esp_mqtt_client_handle_t mqtt_client = {0};
esp_mqtt_client_config_t mqtt_config = {0};
char token[256];  // token was seen to be at least 136 char long

int refresh_token(esp_mqtt_client_config_t *mqtt_config){
    //create_sas_token(1*60*15, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
	create_sas_token(3600, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
	//create_sas_token(1*3600, &token);
    //ESP_LOGE(TAG, "connection token is %s", token);
    mqtt_config->password = token;
    return 0;
}

int publish_to_iothub(const char* payload, const char* topic){
    if(mqtt_client == NULL){
        return -1;
    }

    int message_id = esp_mqtt_client_publish(
            mqtt_client, topic,
            payload, 0, 1, 0
    );

    if(message_id>0){
        return 0;
    }
    ESP_LOGW(TAG, "failed ot add message to mqtt client publish queue");
    return -2;
}

EventGroupHandle_t blocked_publish_event_group;
#define BLOCKED_MESSAGE_PUBLISHED BIT0
#define BLOCKED_MESSAGE_QUEUED BIT1
SemaphoreHandle_t blocked_publish_mutex;
static int blocked_message;

int publish_iothub_event_blocked(const char* payload, TickType_t xTicksToWait){

	int result = -3;

	if(xSemaphoreTake(blocked_publish_mutex, xTicksToWait)!=pdTRUE){
		result = -1;
		goto mutex_err;
	}

	int message_id = esp_mqtt_client_publish(
            mqtt_client, event_topic,
            payload, 0, 1, 0
    );	

	if(message_id<0){
		result = -2;
		goto mutex_err;
	}

	blocked_message = message_id;
	xEventGroupSetBits(blocked_publish_event_group, BLOCKED_MESSAGE_QUEUED);

	EventBits_t flag_field = xEventGroupWaitBits(blocked_publish_event_group, BLOCKED_MESSAGE_PUBLISHED, 1, 1, xTicksToWait);
	if((flag_field&BLOCKED_MESSAGE_PUBLISHED) != 0 ){
		result = 0;
	}

	xEventGroupClearBits(blocked_publish_event_group, BLOCKED_MESSAGE_QUEUED);
	mutex_err:
	xSemaphoreGive(blocked_publish_mutex);
	return result;
}

int publish_iothub_event(const char *payload){
    return publish_to_iothub(payload, event_topic);
}

bool CloudSettingsAreUpdated()
{
	return cloudSettingsAreUpdated;
}

void ClearCloudSettingsAreUpdated()
{
	cloudSettingsAreUpdated = false;
}


bool LocalSettingsAreUpdated()
{
	return localSettingsAreUpdated;
}

void ClearLocalSettingsAreUpdated()
{
	localSettingsAreUpdated = false;
}


bool GetReportGridTestResults()
{
	return reportGridTestResults;
}

void ClearReportGridTestResults()
{
	reportGridTestResults = false;
}


bool GetMCUDiagnosticsResults()
{
	return MCUDiagnosticsResults;
}

void ClearMCUDiagnosicsResults()
{
	MCUDiagnosticsResults = false;
}

bool GetESPDiagnosticsResults()
{
	return ESPDiagnosticsResults;
}

void ClearESPDiagnosicsResults()
{
	ESPDiagnosticsResults = false;
}


bool GetInstallationConfigOnFile()
{
	return reportInstallationConfigOnFile;
}

void ClearInstallationConfigOnFile()
{
	reportInstallationConfigOnFile = false;
}


bool newInstallationIdFlag = false;
void ClearNewInstallationIdFlag()
{
	newInstallationIdFlag = false;
}
bool GetNewInstallationIdFlag()
{
	return newInstallationIdFlag;
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

	char const separator[2] = ",";
	char * stringPart;

	stringPart = strtok(recvString, separator);

	bool doSave = false;
	int nrOfParameters = 0;

	while(stringPart != NULL)
	{
		nrOfParameters++;

		char * pos = strstr(stringPart, " 120 : ");
		if(pos != NULL)
		{
			int useAuthorization = 0;
			sscanf(pos+strlen(" 120 : "),"%d", &useAuthorization);
			ESP_LOGI(TAG, "120 useAuthorization: %d \n", useAuthorization);

			if((useAuthorization == 0) || (useAuthorization == 1))
			{
				MessageType ret = MCU_SendUint8Parameter(AuthenticationRequired, (uint8_t)useAuthorization);
				if(ret == MsgWriteAck)
				{
					storage_Set_AuthenticationRequired((uint8_t)useAuthorization);
					ESP_LOGI(TAG, "DoSave AuthenticationRequired=%d", useAuthorization);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU useAuthorization parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid useAuthorization: %d \n", useAuthorization);
			}

		}

		pos = strstr(stringPart, " 510 : ");
		if(pos != NULL)
		{
			float currentInMaximum = 0.0;
			sscanf(pos+strlen(" 510 : "),"%f", &currentInMaximum);

			if((32.0 >= currentInMaximum) && (currentInMaximum >= 0.0))
			{
				MessageType ret = MCU_SendFloatParameter(ParamCurrentInMaximum, currentInMaximum);
				if(ret == MsgWriteAck)
				{
					storage_Set_CurrentInMaximum(currentInMaximum);
					ESP_LOGI(TAG, "DoSave 510 currentInMaximum: %f \n", currentInMaximum);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU currentInMaximum parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid currentInMaximum: %f \n", currentInMaximum);
			}

		}

		pos = strstr(stringPart, " 511 : ");
		if(pos != NULL)
		{

			float currentInMinimum = 0.0;
			sscanf(pos+strlen(" 511 : "),"%f", &currentInMinimum);

			if((32.0 >= currentInMinimum) && (currentInMinimum >= 0.0))
			{
				MessageType ret = MCU_SendFloatParameter(ParamCurrentInMinimum, currentInMinimum);
				if(ret == MsgWriteAck)
				{
					storage_Set_CurrentInMinimum(currentInMinimum);
					ESP_LOGI(TAG, "DoSave 511 currentInMinimum: %f \n", currentInMinimum);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU currentInMinimum parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid currentInMinimum: %f \n", currentInMinimum);
			}
		}

		pos = strstr(stringPart, " 520 : ");
		if(pos != NULL)
		{
			int maxPhases = 0;
			sscanf(pos+strlen(" 520 : "),"%d", &maxPhases);

			if((3 >= maxPhases) && (maxPhases >= 1))
			{
				MessageType ret = MCU_SendUint8Parameter(MaxPhases, (uint8_t)maxPhases);
				if(ret == MsgWriteAck)
				{
					storage_Set_MaxPhases((uint8_t)maxPhases);
					ESP_LOGI(TAG, "DoSave 520 maxPhases=%d\n", maxPhases);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU maxPhases parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid maxPhases: %d \n", maxPhases);
			}
		}

		pos = strstr(stringPart, " 522 : ");
		if(pos != NULL)
		{
			int defaultOfflinePhase = 0;
			sscanf(pos+strlen(" 522 : "),"%d", &defaultOfflinePhase);
			ESP_LOGE(TAG, "522 defaultOfflinePhase=%d  - Not used\n", defaultOfflinePhase);
			//if((3 >= defaultOfflinePhase) && (defaultOfflinePhase > 1))
			if((9 >= defaultOfflinePhase) && (defaultOfflinePhase >= 1))
			{
				//MessageType ret = MCU_SendUint8Parameter(ChargerOfflinePhase, (uint8_t)defaultOfflinePhase);
				//if(ret == MsgWriteAck)
				//{
					storage_Set_DefaultOfflinePhase((uint8_t)defaultOfflinePhase);
					ESP_LOGI(TAG, "DoSave 522 defaultOfflinePhase=%d\n", defaultOfflinePhase);
					doSave = true;
				//}
				//else
				//{
				//	ESP_LOGE(TAG, "MCU defaultOfflinePhase parameter error");
				//}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid defaultOfflinePhase: %d \n", defaultOfflinePhase);
			}
		}

		pos = strstr(stringPart, " 523 : ");
		if(pos != NULL)
		{
			float defaultOfflineCurrent = 0.0;
			sscanf(pos+strlen(" 523 : "),"%f", &defaultOfflineCurrent);

			if((32.0 >= defaultOfflineCurrent) && (defaultOfflineCurrent >= 0.0))
			{
				//MessageType ret = MCU_SendFloatParameter(ChargerOfflineCurrent, defaultOfflineCurrent);
				//if(ret == MsgWriteAck)
				//{
				storage_Set_DefaultOfflineCurrent(defaultOfflineCurrent);
				ESP_LOGI(TAG, "DoSave 523 defaultOfflineCurrent: %f \n", defaultOfflineCurrent);
				doSave = true;
				//}
				//else
				//{
					//ESP_LOGE(TAG, "MCU defaultOfflineCurrent parameter error");
				//}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid defaultOfflineCurrent: %f \n", defaultOfflineCurrent);
			}
		}

		pos = strstr(stringPart, " 711 : ");
		if(pos != NULL)
		{
			int isEnabled = 0;
			sscanf(pos+strlen(" 711 : "),"%d", &isEnabled);
			ESP_LOGI(TAG, "711 isEnabled: %d \n", isEnabled);

			if((isEnabled == 0) || (isEnabled == 1))
			{
				MessageType ret = MCU_SendUint8Parameter(ParamIsEnabled, (uint8_t)isEnabled);
				if(ret == MsgWriteAck)
				{
					storage_Set_IsEnabled((uint8_t)isEnabled);
					ESP_LOGI(TAG, "DoSave 711 isEnabled=%d\n", isEnabled);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU isEnabled parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid isEnabled: %d \n", isEnabled);
			}
		}

		pos = strstr(stringPart, " 712 : ");
		if(pos != NULL)
		{
			int standalone = 0;
			sscanf(pos+strlen(" 712 : "),"%d", &standalone);
			ESP_LOGI(TAG, "712 standalone: %d \n", standalone);


			if((standalone == 0) || (standalone == 1))
			{
				MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, (uint8_t)standalone);
				if(ret == MsgWriteAck)
				{
					storage_Set_Standalone((uint8_t)standalone);
					ESP_LOGI(TAG, "DoSave 712 standalone=%d\n", standalone);
					doSave = true;
				}
				else
				{
					ESP_LOGE(TAG, "MCU standalone parameter error");
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid standalone: %d \n", standalone);
			}
		}

		pos = strstr(stringPart, " 800 : ");
		if(pos != NULL)
		{

			char installationId[DEFAULT_STR_SIZE] = {0};
			sscanf(pos+strlen(" 800 : "),"%36s", installationId);//Read Max 36 characters
			ESP_LOGI(TAG, "800 installationId: %s \n", installationId);
			storage_Set_InstallationId(installationId);
			doSave = true;

			newInstallationIdFlag = true;
			//continue;
		}

		pos = strstr(stringPart, " 801 : ");
		if(pos != NULL)
		{

			char routingId[DEFAULT_STR_SIZE] = {0};
			sscanf(pos+strlen(" 801 : "),"%36s", routingId);//Read Max 36 characters
			ESP_LOGI(TAG, "801 routingId: %s \n", routingId);
			storage_Set_RoutingId(routingId);
			doSave = true;

			newInstallationIdFlag = true;
			//continue;
		}

		pos = strstr(stringPart, " 802 : ");
		if(pos != NULL)
		{
			char * nameEnd = strstr(stringPart, " }");
			if(nameEnd != NULL)
			{
				char chargerName[DEFAULT_STR_SIZE] = {0};
				char * nameStart = pos+strlen(" 802 : ");
				int nameLen = nameEnd - nameStart;
				if(nameLen <= 36)
				{
					strncpy(chargerName, nameStart, nameLen);

				//sscanf(pos+strlen(" 802 : "),"%36s", chargerName);//Read Max 36 characters %[^\0]
					ESP_LOGI(TAG, "802 chargerName: %s \n", chargerName);
					storage_Set_ChargerName(chargerName);
					doSave = true;
				}
			}
			//continue;
		}

		pos = strstr(stringPart, " 805 : ");
		if(pos != NULL)
		{
			uint32_t diagnosticsMode = 0;
			sscanf(pos+strlen(" 805 : "),"%d", &diagnosticsMode);
			ESP_LOGI(TAG, "805 diagnosticsMode: %u \n", diagnosticsMode);
			storage_Set_DiagnosticsMode(eSWAP_COMMUNICATION_MODE);
			doSave = true;
			//continue;
		}

		stringPart = strtok(NULL, separator);
		if(stringPart != NULL)
			ESP_LOGI(TAG, "Str: %s \n", stringPart);

	}


	ESP_LOGI(TAG, "Received %d parameters", nrOfParameters);

	if(doSave == true)
	{
		esp_err_t err = storage_SaveConfiguration();
		ESP_LOGI(TAG, "Saved CloudSettings: %s=%d\n", (err == 0 ? "OK" : "FAIL"), err);
		cloudSettingsAreUpdated = true;
	}
	else
	{
		ESP_LOGI(TAG, "CloudSettings: Nothing to save");
	}

}

void ParseLocalSettingsFromCloud(char * message, int message_len)
{
	if(message_len < 1)
		return;

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

				uint8_t standalone = 0xff;

				if(strstr(stringPart, "system"))
					standalone = 0;
				else if(strstr(stringPart, "standalone"))
					standalone = 1;

				if((standalone == 0) || (standalone == 1))
				{
					MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, standalone);
					if(ret == MsgWriteAck)
					{
						storage_Set_Standalone(standalone);
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved Standalone=%d, %s=%d\n", standalone, (err == 0 ? "OK" : "FAIL"), err);
						localSettingsAreUpdated = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU standalone parameter error");
					}
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
					//MessageType ret = MCU_SendUint8Parameter(ParamStandalonePhase, standalonePhase);
					//if(ret == MsgWriteAck)
					//{
						storage_Set_StandalonePhase(standalonePhase);
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved STANDALONE_PHASE=%d, %s=%d\n", standalonePhase, (err == 0 ? "OK" : "FAIL"), err);
						localSettingsAreUpdated = true;
					/*}
					else
					{
						ESP_LOGE(TAG, "MCU standalone Phase parameter error");
					}*/
				}
				else
				{
					ESP_LOGI(TAG, "Invalid standalonePhase: %d \n", standalonePhase);
				}
				ESP_LOGE(TAG, "Parameter standalonePhase: %d Not defined for Apollo\n", standalonePhase);
			}

			else if(strstr(stringPart, "max_standalone_current"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				float maxStandaloneCurrent = atof(stringPart+1);

				if((32.0 >= maxStandaloneCurrent) && (maxStandaloneCurrent >= 0.0))
				{
					MessageType ret = MCU_SendFloatParameter(StandAloneCurrent, maxStandaloneCurrent);
					if(ret == MsgWriteAck)
					{
						if(storage_Get_StandaloneCurrent() != maxStandaloneCurrent)
						{
							storage_Set_StandaloneCurrent(maxStandaloneCurrent);
							storage_SaveConfiguration();
							ESP_LOGI(TAG, "Saved STANDALONE_CURRENT=%f", maxStandaloneCurrent);
							localSettingsAreUpdated = true;
						}
					}
					else
					{
						ESP_LOGE(TAG, "MCU standalone current parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Invalid standaloneCurrent: %f \n", maxStandaloneCurrent);
				}
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
					//TODO: Value is measured, handle overwrite from cloud
					/*MessageType ret = MCU_SendUint8Parameter(ParamNetworkType, networkType);
					if(ret == MsgWriteAck)
					{
						storage_Set_NetworkType(networkType);
						//storage_Set_StandalonePhase(networkType);//Set same as Network type, (1-relay)
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved NETWORK TYPE=%d, %s=%d\n", networkType, (err == 0 ? "OK" : "FAIL"), err);
						localSettingsAreUpdated = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU NetworkType parameter error");
					}*/
				}
				else
				{
					ESP_LOGI(TAG, "Invalid NetworkType: %d \n", networkType);
				}
			}

			else if(strstr(stringPart, "hmi_brightness"))
			{
				stringPart = strtok(NULL, separator);
				ESP_LOGI(TAG, "Str: %s \n", stringPart);
				int stringValueLen = strlen(stringPart);
				stringPart[stringValueLen-1] = '\0';
				volatile float hmiBrightness = atof(stringPart+1);

				if((1.0 >= hmiBrightness) && (hmiBrightness >= 0.0))
				{
					MessageType ret = MCU_SendFloatParameter(HmiBrightness, hmiBrightness);
					if(ret == MsgWriteAck)
					{
						storage_Set_HmiBrightness(hmiBrightness);
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved HMI_BRIGHTNESS=%f, %s=%d\n", hmiBrightness, (err == 0 ? "OK" : "FAIL"), err);
						localSettingsAreUpdated = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU HmiBrightness parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Invalid HmiBrightness: %f \n", hmiBrightness);
				}

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

				uint8_t lockValue = 0xFF;
				if(strstr(stringPart,"true") || strstr(stringPart,"True"))
				{
					lockValue = 1;
				}
				else if(strstr(stringPart,"false") || strstr(stringPart,"False"))
				{
					lockValue = 0;
				}

				if((lockValue == 0) || (lockValue == 1))
				{
					MessageType ret = MCU_SendUint8Parameter(PermanentCableLock, lockValue);
					if(ret == MsgWriteAck)
					{
						storage_Set_PermanentLock(lockValue);
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved PermanentLock=%d, %s=%d\n", lockValue, (err == 0 ? "OK" : "FAIL"), err);
						localSettingsAreUpdated = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU ParamPermanentCableLock parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Invalid lockValue: %d \n", lockValue);
				}
			}
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
		MessageType ret = MCU_SendCommandId(CommandReset);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU Start command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU Start command FAILED");
		}
	}

	else if(strstr(commandEvent->topic, "iothub/methods/POST/200/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmware\"-command");
		ble_interface_deinit();


		MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");

			//Only start ota if MCU has ack'ed the stop command
			start_segmented_ota();
			//start_ota();
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");
		}

	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/201/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmwareForced\"-command");
		ESP_LOGI(TAG, "TODO: Implement forced");
		ble_interface_deinit();

		MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
		if(ret == MsgCommandAck)
			ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");
		else
			ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");

		//Start ota even if MCU has NOT ack'ed the stop command
		start_segmented_ota();
		//start_ota();
		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/202/"))
	{
		ESP_LOGI(TAG, "Received \"OTA rollback\"-command");
		ESP_LOGE(TAG, "Active partition: %s", OTAReadRunningPartition());

		char commandString[commandEvent->data_len+1];
		commandString[commandEvent->data_len] = '\0';
		strncpy(commandString, commandEvent->data, commandEvent->data_len);

		//TODO perform in separate thread to be able to ack cloud?
		if(strstr(commandString, "factory") != NULL)
			ota_rollback_to_factory();
		else
			ota_rollback();

		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/501/"))
	{
		//return 200; //For testing offline resendRequestTimer in system mode
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

		if((32 >= currentFromCloud) && (currentFromCloud >= 0))
		{
			//Ensure that when a cloud start command is received, the offlineCurrentSent flag must be cleared
			//to allow a offline current to be resent in case we go offline multiple times.
			sessionHandler_ClearOfflineCurrentSent();
			MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, currentFromCloud);
			if(ret == MsgWriteAck)
			{
				responseStatus = 200;
				ESP_LOGE(TAG, "MCU Start: %f PhaseId: %d \n", currentFromCloud, phaseFromCloud);
				MessageType ret = MCU_SendCommandId(CommandStartCharging);
				if(ret == MsgCommandAck)
				{
					responseStatus = 200;
					ESP_LOGI(TAG, "MCU Start command OK");

					HOLD_SetPhases(phaseFromCloud);
					//cloudCommandCurrentUpdated = true;
				}
				else
				{
					responseStatus = 400;
					ESP_LOGI(TAG, "MCU Start command FAILED");
				}
			}
			else
			{
				responseStatus = 400;
				ESP_LOGE(TAG, "MCU Start command FAILED");
			}
		}
		else
		{
			responseStatus = 400;
			ESP_LOGE(TAG, "Start command with invalid current");
		}
	}
	//Stop charging command
	else if(strstr(commandEvent->topic, "iothub/methods/POST/502/"))
	{
		ESP_LOGE(TAG, "MCU Stop");

		//rDATA=null
		MessageType ret = MCU_SendCommandId(CommandStopCharging);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU Stop command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGE(TAG, "MCU Stop command FAILED");
		}
	}

	else if(strstr(commandEvent->topic, "iothub/methods/POST/503/"))
	{
		ESP_LOGI(TAG, "Received \"ReportChargingState\"-command");
		ClearStartupSent();
		responseStatus = 200;
	}

	else if(strstr(commandEvent->topic, "iothub/methods/POST/504/"))
	{
		//rTOPIC=$iothub/methods/POST/504/?$rid=1
		//rDATA=["806b2f4e-54e1-4913-aa90-376e14daedba"]

		if(commandEvent->data_len < 40)
		{

			if(strncmp(commandEvent->data, "[null]",commandEvent->data_len) == 0)
			{
				ESP_LOGE(TAG, "Session cleared");
				return 200;
			}
			else
			{
				ESP_LOGE(TAG, "Too short SessionId received from cloud");
				return -1;
			}
		}

		if ((commandEvent->data[0] != '[') || (commandEvent->data[commandEvent->data_len-1] != ']'))
			return -2;

		char sessionIdString[commandEvent->data_len];
		strncpy(sessionIdString, commandEvent->data+2, commandEvent->data_len-4);
		sessionIdString[commandEvent->data_len-4] = '\0';

		//ESP_LOGI(TAG, "SessionId: %s , len: %d\n", sessionIdString, strlen(sessionIdString));
		chargeSession_SetSessionIdFromCloud(sessionIdString);
		responseStatus = 200;
	}

	//StopChargingFinal = 506
	else if(strstr(commandEvent->topic, "iothub/methods/POST/506/"))
	{
		MessageType ret = MCU_SendCommandId(CommandStopChargingFinal);// = 508
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU CommandStopChargingFinal command OK");
			SetFinalStopActiveStatus(1);
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU CommandStopChargingFinal command FAILED");
		}
	}

	//ResumeCharging = 507
	else if(strstr(commandEvent->topic, "iothub/methods/POST/507/"))
	{
		//ESP_LOGI(TAG, "Charging denied!");
		MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);// = 509
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK");
			SetFinalStopActiveStatus(0);
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command FAILED");
		}
	}

	else if(strstr(commandEvent->topic, "iothub/methods/POST/601/"))
	{
		//ESP_LOGI(TAG, "Charging granted!");

		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU Granted command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU Granted command FAILED");
		}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/602/"))
	{
		//ESP_LOGI(TAG, "Charging denied!");
		MessageType ret = MCU_SendCommandId(CommandAuthorizationDenied);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU Granted command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU Granted command FAILED");
		}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/800/"))
	{

			if(commandEvent->data_len > 4)
			{
				// Ensure to use a string with proper ending
				char commandString[commandEvent->data_len+1];
				commandString[commandEvent->data_len] = '\0';
				strncpy(commandString, commandEvent->data, commandEvent->data_len);

				ESP_LOGI(TAG, "Debug command: %s", commandString);

				//DiagnosticsModes
				if(strstr(commandString,"DiagnosticsMode 0") != NULL)
				{
					storage_Set_DiagnosticsMode(eCLEAR_DIAGNOSTICS_MODE);
					storage_SaveConfiguration();
					responseStatus = 200;
				}
				else if(strstr(commandString,"DiagnosticsMode 1") != NULL)
				{
					storage_Set_DiagnosticsMode(eNFC_ERROR_COUNT);
					storage_SaveConfiguration();
					responseStatus = 200;
				}

				// Connectivity
				else if(strstr(commandString,"Set LTE") != NULL)
				{
					storage_Set_CommunicationMode(eCONNECTION_LTE);
					storage_SaveConfiguration();
					ESP_LOGI(TAG, "Restarting on LTE");
					esp_restart();
				}
				else if(strstr(commandString,"Set Wifi") != NULL)
				{
					if(network_CheckWifiParameters())
					{
						storage_Set_CommunicationMode(eCONNECTION_WIFI);
						storage_SaveConfiguration();

						ESP_LOGI(TAG, "Restarting on Wifi");
						esp_restart();
					}
					else
					{
						ESP_LOGI(TAG, "No valid Wifi parameters");
					}
				}
				else if(strstr(commandString,"Clear Wifi") != NULL)
				{
					storage_clearWifiParameters();

					ESP_LOGI(TAG, "Cleared Wifi parameters");
					responseStatus = 200;
				}


				// Configuration reset
				else if(strstr(commandString,"Configuration reset") != NULL)
				{
					responseStatus = 400;

					MessageType ret = MCU_SendFloatParameter(StandAloneCurrent, 0.0);
					if(ret == MsgWriteAck)
					{
						MessageType ret = MCU_SendFloatParameter(ChargeCurrentInstallationMaxLimit, 0.0);
						if(ret == MsgWriteAck)
						{
							storage_Init_Configuration();
							storage_SaveConfiguration();
							responseStatus = 200;
						}
					}

					ESP_LOGI(TAG, "Configuration reset");
				}


				// Installation reset
				else if(strstr(commandString,"Installation reset") != NULL)
				{
					responseStatus = 400;

					MessageType ret = MCU_SendFloatParameter(StandAloneCurrent, 0.0);
					if(ret == MsgWriteAck)
					{
						MessageType ret = MCU_SendFloatParameter(ChargeCurrentInstallationMaxLimit, 0.0);
						if(ret == MsgWriteAck)
						{
							storage_Set_StandaloneCurrent(6.0);
							storage_Set_MaxInstallationCurrentConfig(0.0);
							storage_Set_PhaseRotation(0);
							storage_SaveConfiguration();
							ESP_LOGI(TAG, "Installation reset");
							responseStatus = 200;
						}
					}
				}


				// Factory reset
				else if(strstr(commandString,"Factory reset") != NULL)
				{
					storage_clearWifiParameters();
					storage_Init_Configuration();
					storage_SaveConfiguration();

					ESP_LOGI(TAG, "Factory reset");
					responseStatus = 200;
				}else if(strstr(commandString, "segmentota") != NULL){

					MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
					if(ret == MsgCommandAck)
						ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");
					else
						ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");

					//start_segmented_ota();
					start_ota();
				}


				// Logging interval, with space expects number in seconds: "LogInterval 60". This is not yet saved.
				else if(strstr(commandString,"LogInterval ") != NULL)
				{
					char *endptr;
					int interval = (int)strtol(commandString+14, &endptr, 10);
					if((86400 > interval) && (interval > 0))
					{
						SetDataInterval(interval);
						ESP_LOGI(TAG, "Setting LogInterval %d", interval);
						responseStatus = 200;
					}
					else
					{
						responseStatus = 400;
					}
				}
				// Logging interval
				else if(strstr(commandString,"LogInterval") != NULL)
				{
					SetDataInterval(0);
					ESP_LOGI(TAG, "Using default LogInterval");
					responseStatus = 200;
				}
				else if(strstr(commandString,"ClearServoCalibration") != NULL)
				{
					ESP_LOGI(TAG, "ClearServoCalibration");
					MessageType ret = MCU_SendCommandId(CommandServoClearCalibration);
					if(ret == MsgCommandAck)
					{
						responseStatus = 200;
						ESP_LOGI(TAG, "MCU cleared servo");
					}
					else
					{
						responseStatus = 400;
						ESP_LOGI(TAG, "MCU servo clear FAILED");
					}
				}

				// Update certificate (without clearing old directly)
				else if(strstr(commandString,"Update certificate") != NULL)
				{
					certifcate_setBundleVersion(0); //Fake old version for test
					certificate_update(0);

					ESP_LOGI(TAG, "Update certificate");
					responseStatus = 200;
				}
				// Clear certificate (results in new update on next start)
				else if(strstr(commandString,"Clear certificate") != NULL)
				{
					certificate_clear();

					ESP_LOGI(TAG, "Clear certificate");
					responseStatus = 200;
				}

				// Set tls error
				else if(strstr(commandString,"Set tls error") != NULL)
				{
					simulateTlsError = true;

					ESP_LOGI(TAG, "Set tls error");
					responseStatus = 200;
				}

				else if(strstr(commandString,"SetMaxInstallationCurrent ") != NULL)
				{
					char *endptr;
					int maxInt = (int)strtol(commandString+28, &endptr, 10);

					float maxInstCurrentConfig = maxInt * 1.0;

					//Sanity check
					if((40.0 >= maxInstCurrentConfig) && (maxInstCurrentConfig >= 0.0))
					{
						float limitedMaxInst = maxInstCurrentConfig;
						if(maxInstCurrentConfig > 32.0)
							limitedMaxInst = 32.0;

						//Never send higher than 32 to MCU
						MessageType ret = MCU_SendFloatParameter(ChargeCurrentInstallationMaxLimit, limitedMaxInst);
						if(ret == MsgWriteAck)
						{
							storage_Set_MaxInstallationCurrentConfig(maxInstCurrentConfig);
							ESP_LOGI(TAG, "Set MaxInstallationCurrentConfig to MCU: %f", maxInstCurrentConfig);
							storage_SaveConfiguration();
							responseStatus = 200;
						}
						else
						{
							responseStatus = 400;
						}
					}
					else
					{
						responseStatus = 400;
					}
				}

				else if(strstr(commandString,"SetPhaseRotation ") != NULL)
				{
					char *endptr;
					int newPhaseRotation = (int)strtol(commandString+19, &endptr, 10);

					//Sanity check
					if((18 >= newPhaseRotation) && (newPhaseRotation >= 0))
					{
							storage_Set_PhaseRotation(newPhaseRotation);
							ESP_LOGI(TAG, "Set PhaseRotation: %i", newPhaseRotation);
							storage_SaveConfiguration();
							responseStatus = 200;
					}
					else
					{
						responseStatus = 400;
					}
				}

				// GetInstallationConfigOnFile
				else if(strstr(commandString,"GetInstallationConfigOnFile") != NULL)
				{

					reportInstallationConfigOnFile = true;
					ESP_LOGI(TAG, "Getting installationConfigOnFile");
					responseStatus = 200;
				}

				// SetNewWifi
				else if(strstr(commandString,"SetNewWifi:") != NULL)
				{
					char * start = strstr(commandString,"{");
					commandString[commandEvent->data_len-1] = '\0';

					char wifiString[commandEvent->data_len-1];
					int nextChar = 0;
					for (int i = 0; i < commandEvent->data_len-2; i++)
					{
						if(start[i] != '\\')
						{
							wifiString[nextChar] = start[i];
							nextChar++;
						}
					}
					wifiString[nextChar] = '\0';

					cJSON *body = cJSON_Parse(wifiString);
					if(body!=NULL){
						if(cJSON_HasObjectItem(body, "Pin")){
							char * pin = cJSON_GetObjectItem(body, "Pin")->valuestring;
							if(strcmp(pin,i2cGetLoadedDeviceInfo().Pin) == 0)
							{
								if(cJSON_HasObjectItem(body, "SSID")){

									char * ssid = cJSON_GetObjectItem(body, "SSID")->valuestring;
									ESP_LOGW(TAG, "SSID: %s", ssid);


									if(cJSON_HasObjectItem(body, "PSK")){

										char * psk = cJSON_GetObjectItem(body, "PSK")->valuestring;
										ESP_LOGW(TAG, "Psk: %s", psk);

										storage_SaveWifiParameters(ssid, psk);
										if(network_CheckWifiParameters())
										{
											network_updateWifi();
											ESP_LOGW(TAG, "Updated Wifi");
											responseStatus = 200;
										}
									}
									else
									{
										responseStatus = 400;
										return false;
									}

								}
								else
								{
									responseStatus = 400;
									return false;
								}

							}
						}
						else
						{
							//Do not continue invalid content
							responseStatus = 400;
							return false;
						}

					}

					cJSON_Delete(body);


					ESP_LOGI(TAG, "Setting new Wifi");
				}

				else if(strstr(commandString,"SwapCommunicationMode") != NULL)
				{
					if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
						storage_Set_CommunicationMode(eCONNECTION_LTE);
					else if(storage_Get_CommunicationMode() == eCONNECTION_LTE)
						storage_Set_CommunicationMode(eCONNECTION_WIFI);

					storage_Set_DiagnosticsMode(eSWAP_COMMUNICATION_MODE);
					storage_SaveConfiguration();

					ESP_LOGI(TAG, "SwapCommunicationMode");
					responseStatus = 200;

					esp_restart();
				}

				else if(strstr(commandString,"ActivateLogging") != NULL)
				{
					esp_log_level_set("*", ESP_LOG_INFO);
					storage_Set_DiagnosticsMode(eACTIVATE_LOGGING);
					storage_SaveConfiguration();

					ESP_LOGI(TAG, "ActivateLogging");
					responseStatus = 200;

				}
				else if(strstr(commandString,"Simulate offline") != NULL)
				{
					sessionHandler_simulateOffline();

					ESP_LOGI(TAG, "Simulate offline");
					responseStatus = 200;
				}

				if(strstr(commandString,"Activate TCP") != NULL)
				{
					storage_Set_DiagnosticsMode(eACTIVATE_TCP_PORT);
					storage_SaveConfiguration();
					responseStatus = 200;
				}

				else if(strstr(commandString,"OverrideNetworkType ") != NULL)
				{
					char *endptr;
					int newNetworkType = (int)strtol(commandString+22, &endptr, 10);

					//Sanity check
					if((4 >= newNetworkType) && (newNetworkType >= 0))
					{
							storage_Set_NetworkTypeOverride(newNetworkType);
							ESP_LOGI(TAG, "Set Override Network type: %i", newNetworkType);
							storage_SaveConfiguration();
							responseStatus = 200;
					}
					else
					{
						responseStatus = 400;
					}
				}
				else if(strstr(commandString,"ITMode") != NULL)
				{
					ESP_LOGI(TAG, "ITMode");
					MessageType ret = MCU_SendCommandId(CommandITSelect);
					if(ret == MsgCommandAck)
					{
						responseStatus = 200;
						ESP_LOGI(TAG, "MCU IT mode switched");
					}
					else
					{
						responseStatus = 400;
						ESP_LOGI(TAG, "MCU IT switch FAILED");
					}
				}
				else if(strstr(commandString,"GetDiagnostics") != NULL)
				{
					ESP_LOGI(TAG, "GetDiagnostics");
					MCUDiagnosticsResults = true;
					responseStatus = 200;
				}
				else if(strstr(commandString,"GetRFIDList") != NULL)
				{
					ESP_LOGI(TAG, "GetRFIDList");
					storage_CreateRFIDbuffer();
					storage_printRFIDTagsOnFile(true);
					ESPDiagnosticsResults = true;
					responseStatus = 200;
				}
				else if(strstr(commandString,"RTC ") != NULL)
				{
					char *endptr;
					int rtc = (int)strtol(commandString+5, &endptr, 10);

					ESP_LOGI(TAG, "RTC %i -> 0x%X", rtc, rtc);
					RTCWriteControl(rtc);

					responseStatus = 200;

				}
				else if(strstr(commandString,"RTC") != NULL)
				{
					SetSendRTC();
				}

				//StopChargingFinal = 506
				else if(strstr(commandString,"Final") != NULL)
				{
					//ESP_LOGI(TAG, "Charging denied!");
					MessageType ret = MCU_SendCommandId(CommandStopChargingFinal);// = 508
					if(ret == MsgCommandAck)
					{
						responseStatus = 200;
						ESP_LOGI(TAG, "MCU CommandStopChargingFinal command OK");
						SetFinalStopActiveStatus(1);
					}
					else
					{
						responseStatus = 400;
						ESP_LOGI(TAG, "MCU CommandStopChargingFinal command FAILED");
					}
					responseStatus = 200;
				}

				//ResumeCharging = 507
				else if(strstr(commandString,"Resume") != NULL)
				{
					//ESP_LOGI(TAG, "Charging denied!");
					MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);// = 509
					if(ret == MsgCommandAck)
					{
						responseStatus = 200;
						ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK");
						SetFinalStopActiveStatus(0);

					}
					else
					{
						responseStatus = 400;
						ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command FAILED");
					}
					responseStatus = 200;
				}
			}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/804/"))
	{
		ESP_LOGI(TAG, "GridTest command");
		MessageType ret = MCU_SendCommandId(CommandRunGridTest);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU Granted command OK");
			reportGridTestResults = true;
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU Granted command FAILED");
		}
		responseStatus = 200;
	}

	return responseStatus;
}


static void BuildLocalSettingsResponse(char * responseBuffer)
{
	//char * data = "\"[Device_Parameters]\\nserial = ZAP000014\\nmid = ZAP000014\\ncommunication_mode = Wifi\\nstandalone_setting = standalone\\nmax_standalone_current = 16.00\\nnetwork_type = TN_3\\nstandalone_phase = 4\\nhmi_brightness = 0.4\\n\\n[Wifi_Parameters]\\nname = xxx\\npassword = <masked>\\n\\n[BLE_Parameters]\\nconnect-pin = 0000\\n\\n[Cable]\\npermanent_lock = False\\n\\n\"";
//	char * data = "\"[Device_Parameters]\\nserial = ZAP000014\\nmid = ZAP000014\\n"
//			"communication_mode = Wifi\\n"
//			"standalone_setting = standalone\\n"
//			"max_standalone_current = 16.00\\n"
//			"network_type = TN_3\\nstandalone_phase = 4\\nhmi_brightness = 0.4\\n\\n[Wifi_Parameters]\\nname = xxx\\npassword = <masked>\\n\\n[BLE_Parameters]\\nconnect-pin = 0000\\n\\n[Cable]\\npermanent_lock = False\\n\\n\"";

	sprintf(responseBuffer, "\"[Device_Parameters]\\nserial = %s\\nmid = %s\\n", i2cGetLoadedDeviceInfo().serialNumber, i2cGetLoadedDeviceInfo().serialNumber);

	if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
		sprintf(responseBuffer+strlen(responseBuffer), "communication_mode = Wifi\\n");
	else if(storage_Get_CommunicationMode() == eCONNECTION_LTE)
		sprintf(responseBuffer+strlen(responseBuffer), "communication_mode = LTE\\n");

	if(storage_Get_Standalone() == 0)
		sprintf(responseBuffer+strlen(responseBuffer), "standalone_setting = system\\n");
	else if(storage_Get_Standalone() == 1)
		sprintf(responseBuffer+strlen(responseBuffer), "standalone_setting = standalone\\n");

	//sprintf(responseBuffer+strlen(responseBuffer), "max_standalone_current = %f\\n", storage_Get_StandaloneCurrent());
	float standaloneCurrent = 0.0;
	ZapMessage rxMsg = MCU_ReadParameter(StandAloneCurrent);
	if((rxMsg.identifier == StandAloneCurrent) && (rxMsg.length == 4) && (rxMsg.type == MsgReadAck))
		standaloneCurrent = GetFloat(rxMsg.data);

	sprintf(responseBuffer+strlen(responseBuffer), "max_standalone_current = %f\\n", standaloneCurrent);
	sprintf(responseBuffer+strlen(responseBuffer), "network_type = %s\\n", MCU_GetGridTypeString());

	if(storage_Get_StandalonePhase() != 0)
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
static bool isFirstConnection = false;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    mqtt_client = event->client;

    if((event->error_handle->esp_tls_stack_err != 0) || (event->error_handle->esp_tls_last_esp_err != 0))
    	ESP_LOGE(TAG, "tls error: %X %X", event->error_handle->esp_tls_stack_err, event->error_handle->esp_tls_last_esp_err);

    if(simulateTlsError)
    {
    	event->error_handle->esp_tls_stack_err = 0x2701;
    }

    if(event->error_handle->esp_tls_stack_err != 0)
    {
    	ESP_LOGE(TAG, "TLS error - Updating certificate");
    	certificate_update(event->error_handle->esp_tls_stack_err);

    	//Must reset
    	if(simulateTlsError == true)
    	{
    		event->error_handle->esp_tls_stack_err = 0;
    		simulateTlsError = false;
    	}
    }

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




        // Request twin data on every startup, but not on every following reconnect
        if(isFirstConnection == true)
        {
			ridNr++;
			char devicetwin_topic[64];
			sprintf(devicetwin_topic, "$iothub/twin/GET/?$rid=%d", ridNr);
			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, NULL, 0, 1, 0);

			isFirstConnection = false;
        }

        // Only show this event on first boot, not on every SAS token expiry with reconnect
		publish_debug_message_event("mqtt connected", cloud_event_level_information);

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
		if(xEventGroupGetBits(blocked_publish_event_group)&BLOCKED_MESSAGE_QUEUED){
			if(event->msg_id == blocked_message){
				ESP_LOGI(TAG, "Blocked message published, returning publish call");
				xEventGroupSetBits(blocked_publish_event_group, BLOCKED_MESSAGE_PUBLISHED);
			}else{
				ESP_LOGD(TAG, "Published message while a blocking message was pending");
			}
		}
		
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        //printf("rTOPIC=%.*s\r\n", event->topic_len, event->topic);
        //printf("rDATA=%.*s\r\n", event->data_len, event->data);

        if(strstr(event->topic, "iothub/twin/PATCH/properties/desired/"))
		{
        	ParseCloudSettingsFromCloud(event->data, event->data_len);
		}
        if(strstr(event->topic, "iothub/twin/res/200/"))
        {
        	ParseCloudSettingsFromCloud(event->data, event->data_len);
        }

        if(strstr(event->topic, "iothub/methods/POST/300/"))
        {

        	if(event->data_len > 10)
        	{
        		ParseLocalSettingsFromCloud(event->data, event->data_len);
        	}

        	//Build LocalSettings-response
			char devicetwin_topic[64];
			char ridString[event->topic_len+1];
			strncpy(ridString, event->topic, event->topic_len);
			ridString[event->topic_len] = '\0';
			char * ridSubString = strstr(ridString, "$rid=");

			sprintf(devicetwin_topic, "$iothub/methods/res/200/?%s", ridSubString);

			char responseBuffer[500]={0};//TODO: check length
			BuildLocalSettingsResponse(responseBuffer);
			ESP_LOGW(TAG, "responseStringLength: %d, responseBuffer: %s", strlen(responseBuffer), responseBuffer);

			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, responseBuffer, 0, 1, 0);

        }

        //Handle incoming offline AuthenticationList
        if(strstr(event->topic, "iothub/methods/POST/751/"))
        {
        	if(event->data_len > 10)
        	{
        		//Remove '\\' escape character due to uint8_t->char conversion
        		char rfidList[event->data_len];
        		int nextChar = 0;
        		for (int i = 0; i < event->data_len; i++)
        		{
        			if(event->data[i] != '\\')
        			{
        				rfidList[nextChar] = event->data[i];
        				nextChar++;
        			}
        		}
        		rfidList[nextChar] = '\0';

        		int version = authentication_ParseOfflineList(rfidList, strlen(rfidList));

        		if(version > 0)
        		{
        			int ret = publish_uint32_observation(AuthenticationListVersion, version);
        			ESP_LOGI(TAG, "***** AuthenticationListVersion ret: %d *****", ret);
        		}
//				Debug - for testing authentication
//        		char * messageZer = "{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"*\",\"Action\":0,\"ExpiryDate\":null}]}";
//
//        		//Add 6
//        		char * messageOne = "{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"ble-f9f25dee-29c9-4eb2-af37-9f8e821ba0d9\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"ble-8b06fc14-aa7c-462d-a5d7-a7c943f2c4e0\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-5237AB3B\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-530796E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-034095E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-04C31102F84D80\",\"Action\":0,\"ExpiryDate\":null}]}";
//        		//Remove 1 and 6  - 4 middle left
//        		char * messageTwo = "{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"ble-f9f25dee-29c9-4eb2-af37-9f8e821ba0d9\",\"Action\":1,\"ExpiryDate\":null},{\"Tag\":\"ble-8b06fc14-aa7c-462d-a5d7-a7c943f2c4e0\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-5237AB3B\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-530796E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-034095E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-04C31102F84D80\",\"Action\":1,\"ExpiryDate\":null}]}";
//
//        		//Add 1 a bit different (add) and tag 6 back(add), tag 5 not in, multiple duplicate - 6 in total
//        		char * messageThr = "{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"ble-ffffffff-29c9-4eb2-af37-9f8e821ba0d9\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"ble-8b06fc14-aa7c-462d-a5d7-a7c943f2c4e0\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-5237AB3B\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-530796E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-04C31102F84D80\",\"Action\":0,\"ExpiryDate\":null}]}";
//
//        		ESP_LOGI(TAG, "***** 0 *****");
//        		ParseOfflineAuthenticationList(messageZer, strlen(messageOne));
//
//        		ESP_LOGI(TAG, "***** 1 *****");
//        		ParseOfflineAuthenticationList(messageOne, strlen(messageOne));
//
//        		ESP_LOGI(TAG, "***** 2 *****");
//        		ParseOfflineAuthenticationList(messageTwo, strlen(messageTwo));
//
//        		ESP_LOGI(TAG, "***** 3 *****");
//        		ParseOfflineAuthenticationList(messageThr, strlen(messageThr));

        		//storage_GetStats();

        	}

        	//Build LocalSettings-response
			char devicetwin_topic[64];
			char ridString[event->topic_len+1];
			strncpy(ridString, event->topic, event->topic_len);
			ridString[event->topic_len] = '\0';
			char * ridSubString = strstr(ridString, "$rid=");


			sprintf(devicetwin_topic, "$iothub/methods/res/200/?%s", ridSubString);

			char responseBuffer[500]={0};//TODO: check length
			BuildLocalSettingsResponse(responseBuffer);
			ESP_LOGW(TAG, "responseStringLength: %d, responseBuffer: %s", strlen(responseBuffer), responseBuffer);

			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, responseBuffer, 0, 1, 0);
        }

        //Handle incoming commands
        else if(strstr(event->topic, "iothub/methods/POST/"))
        {
        	int responseStatus = ParseCommandFromCloud(event);

    		char devicetwin_topic[64];

    		char ridString[event->topic_len+1];

    		strncpy(ridString, event->topic, event->topic_len);
    		ridString[event->topic_len] = '\0';
    		volatile char * ridSubString = strstr(ridString, "$rid=");

    		sprintf(devicetwin_topic, "$iothub/methods/res/%d/?%s", responseStatus, ridSubString);//200 = OK, 400 = FAIL

    		char * data = NULL;
    		esp_mqtt_client_publish(mqtt_client, devicetwin_topic, data, 0, 1, 0);

        }

        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "About to connect, refreshing the token");
        refresh_token(&mqtt_config);
		ESP_LOGD(TAG, "setting config with the new token");
        esp_mqtt_set_config(mqtt_client, &mqtt_config);
        break;
    case MQTT_EVENT_ERROR:
    	resetCounter++;

    	ESP_LOGI(TAG, "MQTT_EVENT_ERROR: %d/10, Error: %d %X", resetCounter, event->error_handle->esp_tls_stack_err, event->error_handle->esp_tls_stack_err);


    	if((network_WifiIsConnected() == true) || (LteIsConnected() == true))
    	{
    		if(resetCounter == 5)
			{
    			ESP_LOGI(TAG, "Refreshing Wifi Connected");
    			network_updateWifi();
			}

			if((resetCounter == 10) || (resetCounter == 20) || (resetCounter == 30) || (resetCounter == 40))
			{

				esp_err_t rconErr = esp_mqtt_client_reconnect(mqtt_client);
				ESP_LOGI(TAG, "MQTT event reconnect! Error: %d", rconErr);
			}
    	}
    	else if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
    	{
    		if((resetCounter == 2) || (resetCounter == 10))
			{
				ESP_LOGI(TAG, "Refreshing Wifi UnConnected");
				network_updateWifi();
			}
    	}
    	else
    	{
    		ESP_LOGI(TAG, "No Wifi or LTE");
    	}

    	if(resetCounter == 50)
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

	/*esp_err_t err = esp_tls_init_global_ca_store();
	if(err != ESP_OK)
		printf("Creating store failed: %i\n", err);

	int certlen = sizeof(cert)/sizeof(cert[0]);
	err = esp_tls_set_global_ca_store(cert, certlen);
	if(err != ESP_OK)
		printf("Creating store failed: %i\n", err);
*/
    static char broker_url[128] = {0};
    sprintf(broker_url, "mqtts://%s", MQTT_HOST);

    static char username[128];
    sprintf(username, "%s/%s/?api-version=2018-06-30", MQTT_HOST, cloudDeviceInfo.serialNumber);

    char * instId = storage_Get_InstallationId();
    //volatile int instIdLen = strlen(instId);

    int compare = strncmp(instId, INSTALLATION_ID, 36);
    if(compare != 0)
    	sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), storage_Get_InstallationId());
    else
        sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, ROUTING_ID, INSTALLATION_ID);

    ESP_LOGW(TAG,"event_topic: %s ", event_topic);

    ESP_LOGI(TAG,
        "mqtt connection:\r\n"
        " > uri: %s\r\n"
        " > port: %d\r\n"
        " > username: %s\r\n"
        " > client id: %s\r\n",
        //" > cert_pem len: %d\r\n",
        //" > cert_pem: %s\r\n",
        broker_url, MQTT_PORT, username, cloudDeviceInfo.serialNumber
       // strlen(cert)//, cert
    );

    mqtt_config.uri = broker_url;
    mqtt_config.event_handle = mqtt_event_handler;
    mqtt_config.port = MQTT_PORT;
    mqtt_config.username = username;
    mqtt_config.client_id = cloudDeviceInfo.serialNumber;
    //mqtt_config.cert_pem = cert;
    mqtt_config.use_global_ca_store = true;

    mqtt_config.lwt_qos = 1;
    mqtt_config.lwt_topic = event_topic;
    static char *lwt = "{\"EventType\":30,\"Message\":\"mqtt connection broke[lwt]\",\"Type\":5}";
    mqtt_config.lwt_msg = lwt;

    mqtt_config.disable_auto_reconnect = false;
    mqtt_config.reconnect_timeout_ms = 10000;
    mqtt_config.keepalive = 300;//120;
    //mqtt_config.refresh_connection_after_ms = 30000;

	blocked_publish_event_group = xEventGroupCreate();
	xEventGroupClearBits(blocked_publish_event_group, BLOCKED_MESSAGE_PUBLISHED);
	xEventGroupClearBits(blocked_publish_event_group, BLOCKED_MESSAGE_QUEUED);
	blocked_publish_mutex = xSemaphoreCreateMutex();

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_LOGI(TAG, "starting mqtt");
    esp_mqtt_client_start(mqtt_client);
}

void stop_cloud_listener_task()
{
	MqttSetDisconnected();
	esp_mqtt_client_disconnect(mqtt_client);
	esp_mqtt_client_stop(mqtt_client);
	esp_mqtt_client_destroy(mqtt_client);
}

void update_installationId()
{

    char * instId = storage_Get_InstallationId();

    int compare = strncmp(instId, INSTALLATION_ID, 36);
    if(compare != 0)
    	sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), storage_Get_InstallationId());
    else
        sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, ROUTING_ID, INSTALLATION_ID);

    ESP_LOGW(TAG,"New event_topic: %s ", event_topic);

    //mqtt_config.cert_pem = cert;

    mqtt_config.lwt_topic = event_topic;
    /*esp_mqtt_client_disconnect(mqtt_client);
    esp_mqtt_client_stop(mqtt_client);
	esp_mqtt_set_config(mqtt_client, &mqtt_config);
	esp_mqtt_client_start(mqtt_client);
	refresh_token(&mqtt_config);*/
}

void periodic_refresh_token()
{
	ESP_LOGW(TAG, "Periodic new token");
	refresh_token(&mqtt_config);
    esp_mqtt_set_config(mqtt_client, &mqtt_config);

    /*esp_mqtt_client_disconnect(mqtt_client);
    esp_err_t rconErr = esp_mqtt_client_reconnect(mqtt_client);
    ESP_LOGI(TAG, "MQTT reconnect error? %d", rconErr);*/
}
