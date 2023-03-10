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
#include "segmented_ota.h"
#include "safe_ota.h"
#include "ble_interface.h"
#include "../cellular_modem/include/ppp_task.h"
#include "../wifi/include/network.h"
#include "../../main/certificate.h"
#include "../ble/ble_service_wifi_config.h"
#include "../authentication/rfidPairing.h"
#include "../../main/offline_log.h"
#include "../../main/offlineHandler.h"
#include "../../main/offlineSession.h"
#include "../../main/chargeController.h"
#include "../../main/production_test.h"
#include "fat.h"

#include "esp_tls.h"
#include "base64.h"
#include "main.h"
#include <math.h>

#define TAG "CLOUD LISTENER "

#ifdef DEVELOPEMENT_URL
	#define MQTT_HOST CONFIG_ZAPTEC_CLOUD_URL_DEVELOPMENT_MQTT //FOR DEVELOPEMENT
#else
	#define MQTT_HOST CONFIG_ZAPTEC_CLOUD_URL_MAIN_MQTT
#endif

#define MQTT_PORT 8883

#define MQTT_USERNAME_PATTERN "%s/%s/?api-version=2018-06-30"
#define MQTT_EVENT_PATTERN "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8&ri=%s&ii=%s"
#define MQTT_EVENT_PATTERN_PING_REPLY "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8&ri=%s&ii=%s&pi=%s"

static int resetCounter = 0;
static int reconnectionAttempt = 0;

static char event_topic[128];
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

static int rfidListIsUpdated = -1;


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
    //create_sas_token(30, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
	create_sas_token(604800, cloudDeviceInfo.serialNumber, cloudDeviceInfo.PSK, (char *)&token);
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

    //ESP_LOGE(TAG, "<<<sending>>> Id %i - Len: %d: %s", message_id, strlen(payload), payload);

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

int RFIDListIsUpdated()
{
	return rfidListIsUpdated;
}

void ClearRfidListIsUpdated()
{
	rfidListIsUpdated = -1;
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

static bool datalog = false;
bool GetDatalog()
{
	return datalog;
}


void ParseCloudSettingsFromCloud(char * message, int message_len)
{
	if ((message[0] != '{') || (message[message_len-1] != '}'))
		return;

	ESP_LOGW(TAG, "message: %.*s", message_len, message);

	ESP_LOGI(TAG, "***** Start parsing of Cloud settings *****\n");

	bool doSave = false;
	int nrOfParameters = 0;

	cJSON *cloudObject = cJSON_Parse(message);//"{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"ble-f9f25dee-29c9-4eb2-af37-9f8e821ba0d9\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"ble-8b06fc14-aa7c-462d-a5d7-a7c943f2c4e0\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-5237AB3B\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-530796E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-034095E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-04C31102F84D80\",\"Action\":0,\"ExpiryDate\":null}]}");
	cJSON *desiredObject = NULL;
	cJSON *settings = NULL;

	//The settings in the message may start on two different levels, handle both
	if(cJSON_HasObjectItem(cloudObject, "desired"))
	{
		desiredObject = cJSON_GetObjectItem(cloudObject,"desired");
		if(cJSON_HasObjectItem(desiredObject, "Settings"))
			settings = cJSON_GetObjectItem(desiredObject,"Settings");
	}
	else if(cJSON_HasObjectItem(cloudObject, "Settings"))
	{
		settings = cJSON_GetObjectItem(cloudObject,"Settings");
	}

	if(settings != NULL)
	{

		//Authorization
		if(cJSON_HasObjectItem(settings, "120"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"120")->valuestring;
			//ESP_LOGI(TAG, "120 Authorization=%s", valueString);

			int useAuthorization = atoi(valueString);

			//ESP_LOGI(TAG, "120 useAuthorization: %d", useAuthorization);

			if((useAuthorization == 0) || (useAuthorization == 1))
			{
				//Only save if different from value on file
				if(useAuthorization != (int)storage_Get_AuthenticationRequired())
				{
					MessageType ret = MCU_SendUint8Parameter(AuthenticationRequired, (uint8_t)useAuthorization);
					if(ret == MsgWriteAck)
					{
						storage_Set_AuthenticationRequired((uint8_t)useAuthorization);
						ESP_LOGW(TAG, "New: 120 AuthenticationRequired=%d", useAuthorization);
						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU useAuthorization parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 120 Authorization %d", storage_Get_AuthenticationRequired());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid useAuthorization: %d \n", useAuthorization);
			}
		}


		//Maximum current
		if(cJSON_HasObjectItem(settings, "510"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"510")->valuestring;
			//ESP_LOGI(TAG, "510 MaxCurrent=%s", valueString);

			float currentInMaximum = atof(valueString);

			if((32.0 >= currentInMaximum) && (currentInMaximum >= 0.0))
			{
				if(currentInMaximum != storage_Get_CurrentInMaximum())
				{
					MessageType ret = MCU_SendFloatParameter(ParamCurrentInMaximum, currentInMaximum);
					if(ret == MsgWriteAck)
					{
						storage_Set_CurrentInMaximum(currentInMaximum);
						ESP_LOGW(TAG, "New: 510 currentInMaximum: %f \n", currentInMaximum);
						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU currentInMaximum parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 510 Maximum current: %f", storage_Get_CurrentInMaximum());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid currentInMaximum: %f \n", currentInMaximum);
			}
		}


		///Minimum current
		if(cJSON_HasObjectItem(settings, "511"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"511")->valuestring;
			//ESP_LOGI(TAG, "511 MinCurrent=%s", valueString);

			float currentInMinimum = atof(valueString);

			if((32.0 >= currentInMinimum) && (currentInMinimum >= 0.0))
			{
				if(currentInMinimum != storage_Get_CurrentInMinimum())
				{
					MessageType ret = MCU_SendFloatParameter(ParamCurrentInMinimum, currentInMinimum);
					if(ret == MsgWriteAck)
					{
						storage_Set_CurrentInMinimum(currentInMinimum);
						ESP_LOGW(TAG, "New: 511 currentInMinimum: %f \n", currentInMinimum);
						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU currentInMinimum parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 511 Minimum current: %f", storage_Get_CurrentInMinimum());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid currentInMinimum: %f \n", currentInMinimum);
			}
		}


		//MaxPhases
		if(cJSON_HasObjectItem(settings, "520"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"520")->valuestring;
			//ESP_LOGI(TAG, "520 MaxPhases=%s", valueString);

			int maxPhases = atoi(valueString);

			//Since this is not a setting like on Pro, but a measurement Go sends to cloud, we don't need to save it or send to MCU
			//Just compare and see if measured value matches value received from Cloud.

			if(maxPhases != GetMaxPhases())
			{
				ESP_LOGE(TAG, "520 MaxPhases: %d !=%d -> Differ!!!", maxPhases, GetMaxPhases());
			}
			else
			{
				ESP_LOGI(TAG, "NA : 520 MaxPhases: %d == %d -> OK", maxPhases, GetMaxPhases());
			}

			/*if((3 >= maxPhases) && (maxPhases >= 1))
			{
				if(maxPhases != (int)storage_Get_MaxPhases())
				{
					MessageType ret = MCU_SendUint8Parameter(MaxPhases, (uint8_t)maxPhases);
					if(ret == MsgWriteAck)
					{
						storage_Set_MaxPhases((uint8_t)maxPhases);
						ESP_LOGW(TAG, "New: 520 maxPhases=%d\n", maxPhases);
						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU maxPhases parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 520 MaxPhases: %d", storage_Get_MaxPhases());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid maxPhases: %d \n", maxPhases);
			}*/

		}

		//DefaultOfflinePhase
		if(cJSON_HasObjectItem(settings, "522"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"522")->valuestring;
			//ESP_LOGI(TAG, "522 DefaultOfflinePhase=%s", valueString);

			int defaultOfflinePhase = atoi(valueString);

			//ESP_LOGE(TAG, "522 defaultOfflinePhase=%d\n", defaultOfflinePhase);

			if((9 >= defaultOfflinePhase) && (defaultOfflinePhase >= 1))
			{
				//MessageType ret = MCU_SendUint8Parameter(ChargerOfflinePhase, (uint8_t)defaultOfflinePhase);
				//if(ret == MsgWriteAck)
				//{
				if(defaultOfflinePhase != (int)storage_Get_DefaultOfflinePhase())
				{
					storage_Set_DefaultOfflinePhase((uint8_t)defaultOfflinePhase);
					ESP_LOGW(TAG, "New: 522 defaultOfflinePhase=%d\n", defaultOfflinePhase);
					doSave = true;
				}
				else
				{
					ESP_LOGI(TAG, "Old: 522 OfflinePhase: %d", storage_Get_DefaultOfflinePhase());
				}
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

		//DefaultOfflineCurrent
		if(cJSON_HasObjectItem(settings, "523"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"523")->valuestring;
			//ESP_LOGI(TAG, "523 DefaultOfflineCurrent=%s", valueString);

			float defaultOfflineCurrent = atof(valueString);

			if((32.0 >= defaultOfflineCurrent) && (defaultOfflineCurrent >= 0.0))
			{
				//MessageType ret = MCU_SendFloatParameter(ChargerOfflineCurrent, defaultOfflineCurrent);
				//if(ret == MsgWriteAck)
				//{
				if(defaultOfflineCurrent != storage_Get_DefaultOfflineCurrent())
				{
					storage_Set_DefaultOfflineCurrent(defaultOfflineCurrent);
					ESP_LOGW(TAG, "New: 523 defaultOfflineCurrent: %f \n", defaultOfflineCurrent);
					doSave = true;
				}
				else
				{
					ESP_LOGI(TAG, "Old: 523 OfflineCurrent: %f", storage_Get_DefaultOfflineCurrent());
				}
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

		//IsEnable
		if(cJSON_HasObjectItem(settings, "711"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"711")->valuestring;
			//ESP_LOGI(TAG, "711 Isenabled=%s", valueString);

			int isEnabled = atoi(valueString);
			//ESP_LOGI(TAG, "711 isEnabled: %d \n", isEnabled);

			if((isEnabled == 0) || (isEnabled == 1))
			{
				if(isEnabled != (int)storage_Get_IsEnabled())
				{
					MessageType ret = MCU_SendUint8Parameter(ParamIsEnabled, (uint8_t)isEnabled);
					if(ret == MsgWriteAck)
					{
						storage_Set_IsEnabled((uint8_t)isEnabled);
						ESP_LOGW(TAG, "New: 711 isEnabled=%d\n", isEnabled);
						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU isEnabled parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 711 IsEnabled: %d ", storage_Get_IsEnabled());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid isEnabled: %d \n", isEnabled);
			}


		}

		//Standalone
		if(cJSON_HasObjectItem(settings, "712"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"712")->valuestring;
			//ESP_LOGI(TAG, "712 Standalone=%s", valueString);

			int standalone = atoi(valueString);

			//ESP_LOGI(TAG, "712 standalone: %d \n", standalone);

			if((standalone == 0) || (standalone == 1))
			{
				if(standalone != (int)storage_Get_Standalone())
				{
					//MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, (uint8_t)standalone);
					//if(ret == MsgWriteAck)
					if(chargeController_SetStandaloneState(standalone))
					{
						//storage_Set_Standalone((uint8_t)standalone);
						ESP_LOGW(TAG, "New: 712 standalone=%d\n", standalone);

						cloud_listener_SetMQTTKeepAliveTime(standalone);

						doSave = true;
					}
					else
					{
						ESP_LOGE(TAG, "MCU standalone parameter error");
					}
				}
				else
				{
					ESP_LOGI(TAG, "Old: 712 Standalone: %d", storage_Get_Standalone());
				}
			}
			else
			{
				ESP_LOGI(TAG, "Invalid standalone: %d \n", standalone);
			}
		}

		//InstallationId
		if(cJSON_HasObjectItem(settings, "800"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"800")->valuestring;
			//ESP_LOGI(TAG, "800 InstallationId=%s", valueString);

			char installationId[DEFAULT_STR_SIZE] = {0};

			//Ensure string is not longer than buffer
			if(strlen(valueString) < DEFAULT_STR_SIZE)
				strcpy(installationId, valueString);

			int instLen = strlen(installationId);
			if(instLen < DEFAULT_STR_SIZE)
			{
				int cmpResult = strcmp(installationId, storage_Get_InstallationId());
				if(cmpResult != 0)
				{
					ESP_LOGW(TAG, "800 installationId: %s \n", installationId);
					storage_Set_InstallationId(installationId);
					doSave = true;

					newInstallationIdFlag = true;
				}
				else
				{
					ESP_LOGI(TAG, "Old: 800 InstallationId: %s", storage_Get_InstallationId());
				}
			}

		}

		//RoutingId
		if(cJSON_HasObjectItem(settings, "801"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"801")->valuestring;
			//ESP_LOGI(TAG, "801 RoutingId=%s", valueString);

			char routingId[DEFAULT_STR_SIZE] = {0};

			//Ensure string is not longer than buffer
			if(strlen(valueString) < DEFAULT_STR_SIZE)
				strcpy(routingId, valueString);

			int riLen = strlen(routingId);
			if(riLen < DEFAULT_STR_SIZE)
			{
				int cmpResult = strcmp(routingId, storage_Get_RoutingId());
				if(cmpResult != 0)
				{
					ESP_LOGW(TAG, "New: 801 RoutingId: %s", routingId);
					storage_Set_RoutingId(routingId);
					doSave = true;

					newInstallationIdFlag = true;
				}
				else
				{
					ESP_LOGI(TAG, "Old: 801 RoutingId: %s", storage_Get_RoutingId());
				}
			}
		}

		//ChargerName
		if(cJSON_HasObjectItem(settings, "802"))
		{
			nrOfParameters++;

			char * valueString = cJSON_GetObjectItem(settings,"802")->valuestring;
			//ESP_LOGI(TAG, "802 ChargerName=%s", valueString);


			if(valueString != NULL)
			{
				char chargerName[DEFAULT_STR_SIZE] = {0};

				//Ensure string is not longer than buffer
				if(strlen(valueString) < DEFAULT_STR_SIZE)
					strcpy(chargerName, valueString);

				int nameLen = strlen(chargerName);
				if(nameLen < DEFAULT_STR_SIZE)
				{
					int cmpResult = strcmp(chargerName, storage_Get_ChargerName());
					if(cmpResult != 0)
					{
						ESP_LOGW(TAG, "New: 802 ChargerName: %s", chargerName);
						storage_Set_ChargerName(chargerName);
						doSave = true;
					}
					else
					{
						ESP_LOGI(TAG, "Old: 802 ChargerName: %s", storage_Get_ChargerName());
					}
				}
			}
		}

		//Due to this being set to SWAP_COMMUNICATION_MODE in earlier versions up to 0.0.1.22,
		//diagnosticsMode must never be set as Cloud parameter, only through command.


		if(cJSON_HasObjectItem(settings, "805"))
		{
			nrOfParameters++;

			ESP_LOGE(TAG, "#### 805 DiagnosticsMode: DO NOT USE ####");
		}

		ESP_LOGI(TAG, "***** End parsing of Cloud settings *****\n");
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

	if(cloudObject != NULL)
		cJSON_Delete(cloudObject);
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
					//MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, standalone);
					//if(ret == MsgWriteAck)
					if(chargeController_SetStandaloneState(standalone))
					{
						//storage_Set_Standalone(standalone);
						esp_err_t err = storage_SaveConfiguration();
						ESP_LOGI(TAG, "Saved Standalone=%d, %s=%d\n", standalone, (err == 0 ? "OK" : "FAIL"), err);

						cloud_listener_SetMQTTKeepAliveTime(standalone);

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
					//TODO: Value is measured, handle overwrite from cloud. Must include MCU to hande IT3-phase correctly
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
static bool rollbackCmdReceived = false;

void cloud_listener_check_cmd()
{
	if(restartCmdReceived == true)
	{
		vTaskDelay(pdMS_TO_TICKS(3000));
		esp_restart();
	}

	if(rollbackCmdReceived == true)
	{
		vTaskDelay(pdMS_TO_TICKS(3000));
		ota_rollback();
		rollbackCmdReceived = false;
	}
}



int InitiateOTASequence()
{
	int status = 400;

	ble_interface_deinit();

	MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
	if(ret == MsgCommandAck)
	{
		status = 200;
		ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");

		//Only start ota if MCU has ack'ed the stop command
		//start_segmented_ota();
		start_safe_ota();
		//start_ota();
	}
	else
	{
		status = 400;
		ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");
	}
	return status;
}

static bool otaDelayActive = false;
bool IsOTADelayActive()
{
	return otaDelayActive;
}
void ClearOTADelay()
{
	otaDelayActive = false;
}

static bool blockStartToTestPingReply = false;
static bool blockPingReply = false;

int ParseCommandFromCloud(esp_mqtt_event_handle_t commandEvent)
{
	int responseStatus = 0;

	//Don't spend time in this function, must return from mqtt-event. May need separate process
	if(strstr(commandEvent->topic, "iothub/methods/POST/1/"))
	{

		if(blockPingReply)
		{
			ESP_LOGW(TAG, "# INCHARGE_PING_REPLY BLOCKED #");
		}
		else
		{
			ESP_LOGW(TAG, "###### INCHARGE_PING_REPLY ######");
			offlineHandler_UpdatePingReplyState(PING_REPLY_ONLINE);
		}

		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/102/"))
	{
		ESP_LOGI(TAG, "Received \"Restart ESP32\"-command");
		//Execute delayed in another thread to allow command ack to be sent to cloud

		MessageType ret = MCU_SendCommandId(CommandReset);
		if(ret == MsgCommandAck)
		{
			restartCmdReceived = true;
			responseStatus = 200;
			storage_Set_And_Save_DiagnosticsLog("#10 Cloud restart command");
			ESP_LOGI(TAG, "MCU reset command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU reset command FAILED");
		}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/103/"))
	{
		ESP_LOGI(TAG, "Received \"Restart MCU\"-command");
		MessageType ret = MCU_SendCommandId(CommandReset);
		if(ret == MsgCommandAck)
		{
			responseStatus = 200;
			ESP_LOGI(TAG, "MCU reset command OK");
		}
		else
		{
			responseStatus = 400;
			ESP_LOGI(TAG, "MCU reset command FAILED");
		}
	}

	else if(strstr(commandEvent->topic, "iothub/methods/POST/200/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmware\"-command");

		if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_DISCONNECTED)
		{

			responseStatus = InitiateOTASequence();
		}
		else
		{
			otaDelayActive = true;
			responseStatus = 200;
			ESP_LOGW(TAG, "OTA Delayed start");
		}

	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/201/"))
	{
		ESP_LOGI(TAG, "Received \"UpgradeFirmwareForced\"-command");

		responseStatus = InitiateOTASequence();

		ESP_LOGW(TAG, "OTA forced: %d", responseStatus);
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/202/"))
	{
		ESP_LOGI(TAG, "Received \"OTA rollback\"-command");
		ESP_LOGE(TAG, "Active partition: %s", OTAReadRunningPartition());

		char commandString[commandEvent->data_len+1];
		commandString[commandEvent->data_len] = '\0';
		strncpy(commandString, commandEvent->data, commandEvent->data_len);

		if(strstr(commandString, "factory") != NULL)
			restartCmdReceived = ota_rollback_to_factory();
		else
			rollbackCmdReceived = true;

		responseStatus = 200;
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/501/"))
	{
		if(blockStartToTestPingReply == true)
		{
			responseStatus = 200;
			return responseStatus;
		}


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
			offlineHandler_ClearOfflineCurrentSent();
			MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, currentFromCloud);
			if(ret == MsgWriteAck)
			{
				responseStatus = 200;
				ESP_LOGW(TAG, "Charge Start from Cloud: %f PhaseId: %d \n", currentFromCloud, phaseFromCloud);

				bool isSent = chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_CLOUD);
				//MessageType ret = MCU_SendCommandId(CommandStartCharging);
				if(isSent)
				{
					//ESP_LOGI(TAG, "MCU Start command OK");

					HOLD_SetPhases(phaseFromCloud);
					sessionHandler_HoldParametersFromCloud(currentFromCloud, phaseFromCloud);

					responseStatus = 200;
				}
				else
				{
					responseStatus = 400;
					//ESP_LOGI(TAG, "MCU Start command FAILED");
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
		ClearStartupSent();				//Trig resend of general and local settings
		cloudSettingsAreUpdated = true; //Trig resend of cloud parameters
		responseStatus = 200;
	}
	/// SetSessionId
	else if(strstr(commandEvent->topic, "iothub/methods/POST/504/"))
	{
		//rTOPIC=$iothub/methods/POST/504/?$rid=1
		//rDATA=["806b2f4e-54e1-4913-aa90-376e14daedba"]

		//Suport null, [null] and "" formatting of empty session
		ESP_LOGW(TAG, "504: Len: %d, %s", commandEvent->data_len, commandEvent->data);
		if(((commandEvent->data_len == 2) && (strncmp(commandEvent->data, "\"\"", 2) == 0)) ||
			((commandEvent->data_len == 4) && (strncmp(commandEvent->data, "[\"\"]", 4) == 0)))
		{
			if((storage_Get_Standalone() == 0))
			{
				sessionHandler_InitiateResetChargeSession();
				chargeSession_HoldUserUUID();
			}
			return 200;
		}


		if(commandEvent->data_len < 40)
		{

			if(strncmp(commandEvent->data, "[null]",commandEvent->data_len) == 0)
			{
				ESP_LOGE(TAG, "No session");
				return 400;
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
		int8_t ret = chargeSession_SetSessionIdFromCloud(sessionIdString);

		//If SessionId has been set before, check if Cloud needs an update of chargerOperatingMode
		if(ret == 1)
			ChargeModeUpdateToCloudNeeded();

		//Return error if the Session was received with no car connected. Can happen in race-condition with short connect-disconnect
		if(ret == -1)
			responseStatus = 400;
		else
			responseStatus = 200;
	}
	/// SetUserUuid
	else if(strstr(commandEvent->topic, "iothub/methods/POST/505/"))
	{
		//rTOPIC=$iothub/methods/POST/504/?$rid=1
		//rDATA=["806b2f4e-54e1-4913-aa90-376e14daedba"]

		//sessionHandler_InitiateResetChargeSession();

		//Clear user UUID
		//if((storage_Get_AuthenticationRequired() == 1) && (storage_Get_Standalone() == 0))//Only in system mode
		if((storage_Get_Standalone() == 0))//Only in system mode
		{
			///Check for cleared userUUID command [""]
			ESP_LOGW(TAG, "505: Len: %d, %s", commandEvent->data_len, commandEvent->data);
			if(((commandEvent->data_len == 2) && (strncmp(commandEvent->data, "\"\"", 2) == 0)) ||
				((commandEvent->data_len == 4) && (strncmp(commandEvent->data, "[\"\"]", 4) == 0)))
			{
				if((storage_Get_Standalone() == 0))
				{
					//Clear session
					SetUUIDFlagAsCleared();
					sessionHandler_InitiateResetChargeSession();
				}
				return 200;
			}

			else if((commandEvent->data_len > 4) && (commandEvent->data_len < 37)) //Auth code length limit
			{
				if((chargeSession_Get().SessionId[0] != '\0') && (storage_Get_AuthenticationRequired() == 1))
				{
					char newAuthCode[37] = {0};
					strncpy(newAuthCode, &commandEvent->data[2], commandEvent->data_len-4);
					MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
					if(ret == MsgCommandAck)
					{
						//chargeSession_SetAuthenticationCode(newAuthCode);
						SetPendingRFIDTag(newAuthCode);
						SetAuthorized(true);
						publish_debug_telemetry_observation_NFC_tag_id(newAuthCode);
						publish_debug_telemetry_observation_ChargingStateParameters();
						ESP_LOGI(TAG, "MCU AuthorizationGranted command OK");
						return 200;
					}
					else
					{
						ESP_LOGE(TAG, "MCU AuthorizationGranted command FAILED");
						return 400;
					}
				}
				else
				{
					return 400;
				}
			}
		}
		else
		{
			return 400;
		}
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
			chargeController_CancelOverride();
			chargeController_SetPauseByCloudCommand(true);
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
			sessionHandler_ClearCarInterfaceResetConditions();
			chargeController_Override();
			chargeController_SetPauseByCloudCommand(false);
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
		//Only in system mode
		if(storage_Get_Standalone() == 0)
		{
			MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
			if(ret == MsgCommandAck)
			{
				responseStatus = 200;
				SetAuthorized(true);
				ESP_LOGI(TAG, "MCU Granted command OK");
			}
			else
			{
				responseStatus = 400;
				ESP_LOGI(TAG, "MCU Granted command FAILED");
			}
		}
		else
		{
			ESP_LOGI(TAG, "Granted from Cloud in standalone");
			responseStatus = 200; //For standalone - don't do anything, just return this responseStatus to make the cloud happy
		}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/602/"))
	{
		//ESP_LOGI(TAG, "Charging denied!");
		//Only in system mode
		if(storage_Get_Standalone() == 0)
		{

			MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_NOT_AUTHORIZED);
			if(ret == MsgWriteAck)
			{
				ESP_LOGI(TAG, "Ack on SESSION_NOT_AUTHORIZED");
			}
			else
			{
				ESP_LOGE(TAG, "NACK on SESSION_NOT_AUTHORIZED");
			}

			ret = MCU_SendCommandId(CommandAuthorizationDenied);
			if(ret == MsgCommandAck)
			{
				responseStatus = 200;
				SetAuthorized(false);
				ESP_LOGI(TAG, "MCU Granted command OK");
			}
			else
			{
				responseStatus = 400;
				ESP_LOGI(TAG, "MCU Granted command FAILED");
			}
		}
		else
		{
			responseStatus = 400;
		}
	}
	else if(strstr(commandEvent->topic, "iothub/methods/POST/750/"))
	{
		rfidPairing_SetState(ePairing_AddedOk);
		ESP_LOGW(TAG, "Command NFC pairing OK");
		responseStatus = 200;
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
			else if(strstr(commandString,"DiagnosticsMode 6") != NULL)
			{
				storage_Set_DiagnosticsMode(eDISABLE_CERTIFICATE_ONCE);
				storage_SaveConfiguration();
				responseStatus = 200;
			}
			else if(strstr(commandString,"DiagnosticsMode 7") != NULL)
			{
				storage_Set_DiagnosticsMode(eDISABLE_CERTIFICATE_ALWAYS);
				storage_SaveConfiguration();
				responseStatus = 200;
			}
			else if(strstr(commandString,"Restart ESP") != NULL)
			{
				restartCmdReceived = true;

				storage_Set_And_Save_DiagnosticsLog("#11 Cloud Restart ESP command");
				responseStatus = 200;
			}

			// Connectivity
			else if(strstr(commandString,"Set LTE") != NULL)
			{
				storage_Set_CommunicationMode(eCONNECTION_LTE);
				storage_SaveConfiguration();
				ESP_LOGI(TAG, "Restarting on LTE");
				restartCmdReceived = true;
				responseStatus = 200;
				//esp_restart();
			}
			else if(strstr(commandString,"Set Wifi") != NULL)
			{
				if(network_CheckWifiParameters())
				{
					storage_Set_CommunicationMode(eCONNECTION_WIFI);
					storage_SaveConfiguration();

					ESP_LOGI(TAG, "Restarting on Wifi");
					restartCmdReceived = true;
					responseStatus = 200;
					//esp_restart();
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

				MessageType ret = MCU_SendUint8Parameter(CommandFactoryReset, 0);
				if(ret == MsgWriteAck) {
					ESP_LOGI(TAG, "MCU Factory Reset OK");
					storage_clearWifiParameters();
					storage_Init_Configuration();
					storage_SaveConfiguration();
					responseStatus = 200;
					ESP_LOGI(TAG, "Factory reset complete");
				}
				else {
					ESP_LOGE(TAG, "MCU Factory Reset FAILED");
					responseStatus=400;
				}
			}else if(strstr(commandString, "segmentota") != NULL){

				MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
				if(ret == MsgCommandAck)
					ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");
				else
					ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");

				//start_segmented_ota();
				start_ota();
			}else if(strstr(commandString, "multiblockota") != NULL){

				MessageType ret = MCU_SendCommandId(CommandHostFwUpdateStart);
				if(ret == MsgCommandAck)
					ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart OK");
				else
					ESP_LOGI(TAG, "MCU CommandHostFwUpdateStart FAILED");

				start_segmented_ota();
			}


			// Logging interval, with space expects number in seconds: "LogInterval 60". This is not yet saved.
			else if(strstr(commandString,"LogInterval ") != NULL)
			{
				char *endptr;
				uint32_t interval = (uint32_t)strtol(commandString+14, &endptr, 10);
				if(((86400 >= interval) && (interval > 10)) || (interval == 0))
				{
					//SetDataInterval(interval);
					storage_Set_TransmitInterval(interval);
					storage_SaveConfiguration();
					ESP_LOGI(TAG, "Setting LogInterval %d", interval);
					responseStatus = 200;
				}
				else
				{
					responseStatus = 400;
				}
			}
			// Logging interval
			/*else if(strstr(commandString,"LogInterval") != NULL)
			{
				//SetDataInterval(0);
				storage_Set_TransmitInterval(3600);
				ESP_LOGI(TAG, "Using default LogInterval");
				responseStatus = 200;
			}*/
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


			else if(strstr(commandString,"Override ") != NULL)
			{
				char *endptr;
				int overrideVersion = strtol(commandString+11, &endptr, 10);
				if((1000 > overrideVersion) && (overrideVersion >=0))
				{
					certifcate_setOverrideVersion(overrideVersion); //Fake old version for test
					certificate_update(0);

					ESP_LOGI(TAG, "Update to override version: %d", overrideVersion);
					responseStatus = 200;
				}
				else
				{
					responseStatus = 400;
				}
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

				restartCmdReceived = true;
				responseStatus = 200;
				//esp_restart();
			}

			else if(strstr(commandString,"ActivateLogging") != NULL)
			{
				esp_log_level_set("*", ESP_LOG_INFO);
				storage_Set_DiagnosticsMode(eACTIVATE_LOGGING);
				storage_SaveConfiguration();

				ESP_LOGI(TAG, "ActivateLogging");
				responseStatus = 200;

			}
			else if(strstr(commandString,"Simulate offline ") != NULL)
			{
				char *endptr;
				int offlineTime = (int)strtol(commandString+19, &endptr, 10);
				if((offlineTime <= 86400) && (offlineTime > 0))
					offlineHandler_SimulateOffline(offlineTime);

				ESP_LOGI(TAG, "Simulate offline %d", offlineTime);
				responseStatus = 200;
			}

			if(strstr(commandString,"Activate TCP") != NULL)
			{
				storage_Set_DiagnosticsMode(eACTIVATE_TCP_PORT);
				storage_SaveConfiguration();
				responseStatus = 200;
			}
			if(strstr(commandString,"AlwaysSendSessionDiagnostics") != NULL)
			{
				storage_Set_DiagnosticsMode(eALWAYS_SEND_SESSION_DIAGNOSTICS);
				storage_SaveConfiguration();
				responseStatus = 200;
			}

			else if(strstr(commandString,"OverrideNetworkType") != NULL)
			{
				//char *endptr;
				int newNetworkType = 0;//(int)strtol(commandString+22, &endptr, 10);
				if(strstr(commandString,"IT1") != NULL)
					newNetworkType = NETWORK_1P3W;
				else if(strstr(commandString,"IT3") != NULL)
					newNetworkType = NETWORK_3P3W;
				else if(strstr(commandString,"TN1") != NULL)
					newNetworkType = NETWORK_1P4W;
				else if(strstr(commandString,"TN3") != NULL)
					newNetworkType = NETWORK_3P4W;

				//Sanity check
				if(IsUKOPENPowerBoardRevision())
				{
					responseStatus = 400;
				}
				else if((4 >= newNetworkType) && (newNetworkType >= 0))
				{
					ESP_LOGI(TAG, "Override Network type to set: %i", newNetworkType);

					MessageType ret = MCU_SendUint8Parameter(ParamGridTypeOverride, newNetworkType);
					if(ret == MsgWriteAck)
					{
						int ret = (int)MCU_UpdateOverrideGridType();

						if(ret == newNetworkType)
						{
							ESP_LOGI(TAG, "Set OverrideNetworkType OK");
							responseStatus = 200;
						}
						else
						{
							ESP_LOGE(TAG, "Set OverrideNetworkType FAILED 1");
							responseStatus = 400;
						}
					}
					else
					{
						ESP_LOGE(TAG, "Set OverrideNetworkType FAILED 2");
					}

					responseStatus = 200;
				}
				else
				{
					responseStatus = 400;
				}
			}

			else if(strstr(commandString,"IT3 enable") != NULL)
			{
				MessageType ret = MCU_SendUint8Parameter(ParamIT3OptimizationEnabled, 1);
				if(ret == MsgWriteAck)
				{
					uint8_t ret = MCU_UpdateIT3OptimizationState();
					if(ret == 0)
					{
						ESP_LOGI(TAG, "Set IT3 optimization enabled OK");
						responseStatus = 200;
					}
					else
					{
						ESP_LOGE(TAG, "Set IT3 optimization enabled FAILED 1");
						responseStatus = 400;
					}
				}
				else
				{
					ESP_LOGE(TAG, "Set IT3 optimization enabled FAILED 2");
					responseStatus = 400;
				}
			}
			else if(strstr(commandString,"IT3 disable") != NULL)
			{
				MessageType ret = MCU_SendUint8Parameter(ParamIT3OptimizationEnabled, 0);
				if(ret == MsgWriteAck)
				{

					uint8_t ret = MCU_UpdateIT3OptimizationState();
					if(ret == 0)
					{
						ESP_LOGI(TAG, "Set IT3 optimization disabled OK");
						responseStatus = 200;
					}
					else
					{
						ESP_LOGE(TAG, "Set IT3 optimization disabled FAILED 1");
						responseStatus = 400;
					}
				}
				else
				{
					ESP_LOGE(TAG, "Set IT3 optimization disabled FAILED 2");
					responseStatus = 400;
				}
			}


			else if(strstr(commandString,"ITStart") != NULL)
			{
				ESP_LOGI(TAG, "IT diagnostics stop");
				MessageType ret = MCU_SendCommandId(CommandITDiagnosticsStart);
				if(ret == MsgCommandAck)
				{
					MCUDiagnosticsResults = true;
					responseStatus = 200;
					ESP_LOGI(TAG, "MCU IT diag ON");
				}
				else
				{
					responseStatus = 400;
					ESP_LOGI(TAG, "MCU IT diag FAILED");
				}
			}
			else if(strstr(commandString,"ITStop") != NULL)
			{
				ESP_LOGI(TAG, "IT diagnostics stop");
				MessageType ret = MCU_SendCommandId(CommandITDiagnosticsStop);
				if(ret == MsgCommandAck)
				{
					MCUDiagnosticsResults = false;
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
			else if(strstr(commandString,"PulseInterval ") != NULL)
			{
				char *endptr;
				uint32_t interval = (uint32_t)strtol(commandString+16, &endptr, 10);
				if((3600 >= interval) && (interval >= 10))
				{
					storage_Set_PulseInterval(interval);
					storage_SaveConfiguration();
					ESP_LOGI(TAG, "Setting Pulse interval %d", interval);
					responseStatus = 200;
				}
				else
				{
					responseStatus = 400;
				}
			}
			else if(strstr(commandString,"PowerOff4GAndReset") != NULL)
			{
				cellularPinsOff();

				//Restart must be done to ensure that we don't remain offline if communication mode is set to 4G.
				//The 4G module will be powered on automatically if 4G is active communication mode
				restartCmdReceived = true;
				responseStatus = 200;
				//esp_restart();
			}

			//For testing AT on BG while on Wifi
			else if(strstr(commandString,"PowerToggle4G") != NULL)
			{
				cellularPinsOff();
			}

			//For testing AT on BG while on Wifi
			else if(strstr(commandString,"PowerOn4G") != NULL)
			{
				cellularPinsOn();
				ATOnly();
			}

			//AT command tunneling - do not change command mode
			else if(strstr(commandString,"AT") != NULL)
			{
				//Don't change data mode when on wifi
				if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
					TunnelATCommand(commandString, 0);

				//Change data mode when on LTE
				if(storage_Get_CommunicationMode() == eCONNECTION_LTE)
					TunnelATCommand(commandString, 1);
			}
			//AT command tunneling - do change command mode
			else if(strstr(commandString,"OnlineWD") != NULL)
			{
				SetOnlineWatchdog();
			}
			//AT command tunneling - do change command mode
			else if(strstr(commandString,"ClearNotifications") != NULL)
			{
				ClearNotifications();
			}

			else if(strstr(commandString,"PrintStat") != NULL)
			{
				char stat[100] = {0};
				storage_GetStats(stat);
				publish_debug_telemetry_observation_Diagnostics(stat);
			}

			else if(strstr(commandString,"DeleteOfflineLog") != NULL)
			{
				int ret = deleteOfflineLog();
				if(ret == 1)
					publish_debug_telemetry_observation_Diagnostics("Delete OK");
				else
					publish_debug_telemetry_observation_Diagnostics("Delete failed");
			}
			else if(strstr(commandString,"StartStack") != NULL)
			{
				//Also send instantly when activated
				SendStacks();
				StackDiagnostics(true);
			}
			else if(strstr(commandString,"StopStack") != NULL)
			{
				StackDiagnostics(false);
			}
			else if(strstr(commandString,"OCMFHigh") != NULL)
			{
				SessionHandler_SetOCMFHighInterval();
			}
			else if(strstr(commandString,"LogCurrent") != NULL)
			{
				int interval = 0;
				sscanf(&commandString[12], "%d", &interval);

				ESP_LOGI(TAG, "Interval: %i", interval);

				if((interval >= 0) && (interval <= 86400))
				{
					SessionHandler_SetLogCurrents(interval);
				}
			}
			else if(strstr(commandString,"RestartCar") != NULL)//MCU Command 507: Reset Car Interface sequence
			{
				MessageType ret = MCU_SendCommandId(MCUCommandRestartCarInterface);
				if(ret == MsgCommandAck)
				{
					responseStatus = 200;
					ESP_LOGI(TAG, "MCU Restart car OK");
				}
				else
				{
					responseStatus = 400;
					ESP_LOGI(TAG, "MCU Restart car FAILED");
				}
			}
			else if(strstr(commandString,"ServoCheck") != NULL)
			{
				MCU_PerformServoCheck();
			}
			else if(strstr(commandString,"GetHWCurrentLimits") != NULL)
			{
				char msg[60] = {0};
				sprintf(msg, "eMeter HW Current limit: %f / %f A", MCU_GetHWCurrentActiveLimit(), MCU_GetHWCurrentMaxLimit());
				publish_debug_telemetry_observation_Diagnostics(msg);
				responseStatus = 200;
			}
			else if(strstr(commandString,"blockreq") != NULL)
			{
				blockStartToTestPingReply = true;
				responseStatus = 200;
			}
			else if(strstr(commandString,"blockall") != NULL)
			{
				blockStartToTestPingReply = true;
				blockPingReply = true;
				responseStatus = 200;
			}
			else if(strstr(commandString,"unblock") != NULL)
			{
				blockStartToTestPingReply = false;
				blockPingReply = false;
				responseStatus = 200;
			}


			else if(strstr(commandString,"pr on") != NULL)
			{
				offlineHandler_UpdatePingReplyState(PING_REPLY_ONLINE);
				responseStatus = 200;
			}
			else if(strstr(commandString,"pr off") != NULL)
			{
				offlineHandler_UpdatePingReplyState(PING_REPLY_OFFLINE);
				responseStatus = 200;
			}

			else if(strstr(commandString,"datalog on") != NULL)
			{
				datalog = true;
				responseStatus = 200;
			}
			else if(strstr(commandString,"datalog off") != NULL)
			{
				datalog = false;
				responseStatus = 200;
			}
			/// This command may not be required, only for troubleshooting if MCU does not respond. Has never happened.
			else if(strstr(commandString,"OTA no MCU") != NULL)
			{
				//Here no command is sent to stop MCU directly.
				ble_interface_deinit();
				start_segmented_ota();
				responseStatus = 200;
			}
			//Run factory test function - dev - disable socket connection
			else if(strstr(commandString,"factest") != NULL)
			{
				run_component_tests();
				responseStatus = 200;
			}


			///OfflineSessions

			else if(strstr(commandString,"GetOfflineSessions") != NULL)
			{
				sessionHandler_SetOfflineSessionFlag();
				responseStatus = 200;
			}
			else if(strstr(commandString,"DeleteOfflineSessions") != NULL)
			{
				offlineSession_DeleteAllFiles();
				responseStatus = 200;
			}
			else if(strstr(commandString,"PrintOffsLog") != NULL)
			{
				ESP_LOGW(TAG, "SequenceLog: \r\n%s", offlineSession_GetLog());
				responseStatus = 200;
			}
			else if(strstr(commandString,"GetNrOfSessionsFiles") != NULL)
			{
				char sbuf[12] = {0};
				snprintf(sbuf, 12,"Files: %i", offlineSession_FindNrOfFiles());
				publish_debug_telemetry_observation_Diagnostics(sbuf);

				responseStatus = 200;
			}
			else if(strstr(commandString,"GetOfflineFile ") != NULL)
			{
				int fileNo = -1;
				sscanf(&commandString[17], "%d", &fileNo);
				if((fileNo >= 0) && (fileNo < 100))
				{
					cJSON * csObject = offlineSession_ReadChargeSessionFromFile(fileNo);
					if(csObject == NULL)
					{
						publish_debug_telemetry_observation_Diagnostics("csObject == NULL");
					}
					else
					{
						char *buf = cJSON_PrintUnformatted(csObject);
						publish_debug_telemetry_observation_Diagnostics(buf);
						free(csObject);

					}
				}

				responseStatus = 200;
			}
			//Test Offline Sessions
			else if(strstr(commandString,"tos ") != NULL)
			{
				char *endptr;
				uint32_t nrOfSessions = (uint32_t)strtol(commandString+6, &endptr, 10);
				if(nrOfSessions <= 110)
				{
					char *sec = strchr(commandString, '|');
					if(sec != NULL)
					{
						uint32_t nrOfSignedValues = (uint32_t)strtol(sec+1, &endptr, 10);
						if(nrOfSignedValues <= 110)
						{
							ESP_LOGW(TAG, "NrSess: %i NrSV: %i", nrOfSessions, nrOfSignedValues);
							sessionHandler_TestOfflineSessions(nrOfSessions, nrOfSignedValues);
						}
					}
				}
				responseStatus = 200;
			}

			/*else if(strstr(commandString,"StartTimer") != NULL)
			{
				//chargeController_SetStartTimer();
				chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_SCHEDULE);
				responseStatus = 200;
			}*/


			else if(strstr(commandString,"Loc ") != NULL)
			{
				//Remove end of string formatting
				int end = strlen(commandString);
				commandString[end-2] = '\0';

				storage_Set_Location(&commandString[6]);
				storage_SaveConfiguration();
				publish_debug_telemetry_observation_TimeAndSchedule(0x7);

				chargeController_Activation();

				responseStatus = 200;
			}

			else if(strstr(commandString,"Tz ") != NULL)
			{
				//Remove end of string formatting
				int end = strlen(commandString);
				commandString[end-2] = '\0';

				storage_Set_Timezone(&commandString[5]);
				storage_SaveConfiguration();
				publish_debug_telemetry_observation_TimeAndSchedule(0x7);

				responseStatus = 200;
			}

			else if(strstr(commandString,"SS") != NULL)
			{
				if (strstr(commandString,"SSID")) {
					// Probably SetNewWifi should handle this..
					return responseStatus;
				}

				//chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_SCHEDULE);

				//Remove end of string formatting
				int end = strlen(commandString);
				commandString[end-2] = '\0';
				if(end >= 19)
				{
					chargeController_WriteNewTimeSchedule(&commandString[4]);
					chargeController_Activation();
					storage_SaveConfiguration();
					chargeController_SetRandomStartDelay();
				}
				else
				{
					char* p = "";
					chargeController_WriteNewTimeSchedule(p);
					chargeController_Activation();
					storage_SaveConfiguration();
					chargeController_ClearRandomStartDelay();
					chargeController_ClearNextStartTime();
					chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_NO_SCHEDULE);
				}
				//chargeController_SetTimes();



				publish_debug_telemetry_observation_TimeAndSchedule(0x7);
				//chargeController_SetStartTimer();
				responseStatus = 200;
			}

			else if(strstr(commandString,"NT") != NULL)
			{
				//chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_SCHEDULE);

				//Remove end of string formatting
				int end = strlen(commandString);
				commandString[end-2] = '\0';

				chargeController_SetNowTime(&commandString[4]);
				responseStatus = 200;
			}
			else if(strstr(commandString,"SIMSTOP") != NULL)
			{
				//505
				sessionHandler_InitiateResetChargeSession();

				//504
				sessionHandler_InitiateResetChargeSession();
				chargeSession_HoldUserUUID();

				//502
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

				responseStatus = 200;
			}
			else if(strstr(commandString,"StartNow") != NULL)
			{
				chargeController_Override();

				responseStatus = 200;
			}

			else if(strstr(commandString,"SchedDiag") != NULL)
			{
				chargeController_SetSendScheduleDiagnosticsFlag();

				responseStatus = 200;
			}

			/*else if(strstr(commandString,"ClearSchedule") != NULL)
			{
				storage_Initialize_ScheduleParameteres();
				storage_SaveConfiguration();
				publish_debug_telemetry_observation_TimeAndSchedule(0x7);

				chargeController_Activation();

				chargeController_ClearNextStartTime();

				responseStatus = 200;
			}*/
			else if(strstr(commandString,"SetSchedule") != NULL)
			{
				if(strstr(commandString,"SetScheduleUK") != NULL)
					storage_Initialize_UK_TestScheduleParameteres();
				else if(strstr(commandString,"SetScheduleNO") != NULL)
					storage_Initialize_NO_TestScheduleParameteres();
				else
					storage_Initialize_ScheduleParameteres();

				storage_SaveConfiguration();
				publish_debug_telemetry_observation_TimeAndSchedule(0x7);

				chargeController_Activation();

				chargeController_ClearNextStartTime();

				responseStatus = 200;
			}

			else if(strstr(commandString,"SetMaxStartDelay ") != NULL)
			{
				int newMaxValue = 0;
				sscanf(&commandString[19], "%d", &newMaxValue);
				if((newMaxValue >= 0) && (newMaxValue <= 3600))
				{
					storage_Set_MaxStartDelay(newMaxValue);
					storage_SaveConfiguration();
					chargeController_SetRandomStartDelay();
				}

				responseStatus = 200;
			}
			else if(strstr(commandString,"GetMCUSettings") != NULL)
			{
				sessionHandler_SendMCUSettings();
				responseStatus = 200;
			}

			else if(strstr(commandString,"GetOPENSamples") != NULL)
			{
				char samples[161] = {0};
				MCU_GetOPENSamples(samples);
				publish_debug_telemetry_observation_Diagnostics(samples);
				responseStatus = 200;
			}
			else if(strstr(commandString,"AbortOTA") != NULL)
			{
				do_segment_ota_abort();
				do_safe_ota_abort();
				responseStatus = 200;
			}
			else if(strstr(commandString,"GetRelayStates") != NULL)
			{
				sessionHandler_SendRelayStates();
				responseStatus = 200;
			}
			else if(strstr(commandString, "CoverProximity"))
			{
				if(strstr(commandString, "SetCoverProximity "))
				{
					int newProxValue = 0;
					sscanf(&commandString[20], "%d", &newProxValue);
					if((newProxValue >= 0) && (newProxValue <= 1000))
					{

						storage_Set_cover_on_value((uint16_t)newProxValue);
						storage_SaveConfiguration();
					}
				}
				else if(strstr(commandString, "GetCoverProximity"))
				{

					char buf[50];
					snprintf(buf, 50, "CoverPriximity: %i", storage_Get_cover_on_value());

					publish_debug_telemetry_observation_Diagnostics(buf);
					responseStatus = 200;
				}
				else if(strstr(commandString, "PrintCoverProximity"))
				{
					tamper_PrintProximity();
					responseStatus = 200;
				}
				else if(strstr(commandString, "SendCoverProximity "))
				{
					int duration = 0;
					sscanf(&commandString[21], "%d", &duration);

					ESP_LOGW(TAG, "Setting duration %i", duration);

					if((duration >= 0) && (duration <= 300000))
					{
						tamper_SendProximity(duration);
						responseStatus = 200;
					}
					else
					{
						responseStatus = 400;
					}
				}
				else if(strstr(commandString, "CalibrateCoverProximity"))
				{
					esp_err_t err = I2CCalibrateCoverProximity();

					switch(err){
					case ESP_OK:
						responseStatus = 200;
						break;
					case ESP_FAIL:
						responseStatus = 500;
						break;
					case ESP_ERR_NOT_SUPPORTED:
						responseStatus = 501; // TODO: See if more appropriate status code exist. (405?)
						break;
					}
				}
			}
			else if(strstr(commandString, "pppoff"))
			{
				ppp_disconnect();
				responseStatus = 200;
			}
			else if(strstr(commandString,"SetOTAChunkSize ") != NULL)
			{
				int newSize = 0;
				sscanf(&commandString[18], "%d", &newSize);
				if((newSize > 64) && (newSize <= (65536*2)))
				{
					ota_set_chunk_size(newSize);
				}

				responseStatus = 200;
			}
			else if(strstr(commandString, "GetFPGAInfo"))
			{
				sessionHandler_SendFPGAInfo();
				responseStatus = 200;
			}
			else if(strstr(commandString, "GetFailedRFID"))
			{
				char atqa[12] = {0};
				uint16_t value = NFCGetLastFailedATQA();
				snprintf(atqa, 12,"ATQA: %02X %02X", ((value>>8) & 0xff), (value & 0xff));
				publish_debug_telemetry_observation_Diagnostics(atqa);
				responseStatus = 200;
			}
			/*else if(strstr(commandString, "getpartitions"))
			{
				char buf[351]={0};
				offlineSession_test_GetPartitions(buf);
				ESP_LOGW(TAG, "Part buf len: %i", strlen(buf));
				publish_debug_telemetry_observation_Diagnostics(buf);
				responseStatus = 200;
			}
			else if(strstr(commandString, "erasefilespartition"))
			{
				esp_partition_t *part  = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "files");

				esp_err_t err = esp_partition_erase_range(part, 0, part->size);

				char partbuf[50];
				snprintf(partbuf, 50, "ErasePartitionResult: %i", err);

				publish_debug_telemetry_observation_Diagnostics(partbuf);
				responseStatus = 200;
			}
			else if(strstr(commandString, "getaccenergy"))
			{
				double accumulated_energy = OCMF_Write_Read_accumulated_energy(0.0);
				ESP_LOGW(TAG, "Read accumulated energy: %f", accumulated_energy);
				char accbuf[50];
				snprintf(accbuf, 40, "ReadEnergy: %f", accumulated_energy);
				publish_debug_telemetry_observation_Diagnostics(accbuf);
			}
			else if(strstr(commandString, "setaccenergy"))
			{
				float newEnergy = 0;
				sscanf(&commandString[14], "%f", &newEnergy);

				double accumulated_energy = OCMF_Write_Read_accumulated_energy(newEnergy);
				ESP_LOGW(TAG, "Wrote accumulated energy: %f", accumulated_energy);
				char accbuf[50];
				snprintf(accbuf, 40, "WroteEnergy: %f", accumulated_energy);
				publish_debug_telemetry_observation_Diagnostics(accbuf);
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}

			else if(strstr(commandString, "getmount"))
			{
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "testmount"))
			{
				offlineSession_mount_folder();
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}*/
			/*else if(strstr(commandString, "testcreate"))
			{
				offlineSession_test_Createfile();
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "testwrite"))
			{
				offlineSession_test_Writefile();
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "testread"))
			{
				offlineSession_test_Readfile();
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "testdelete"))
			{
				offlineSession_test_Deletefile();
				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "readsessionfile"))
			{
				offlineSession_Diagnostics_ReadFileContent(0);

				publish_debug_telemetry_observation_Diagnostics(offlineSession_test_GetFileDiagnostics());
				responseStatus = 200;
			}*/
			else if(strstr(commandString, "FixPartition"))
			{
				if(strstr(commandString, "FixPartitionFilesCheck"))
				{
					offlineSession_ClearDiagnostics();
					offlineSession_CheckFilesSystem();
					publish_debug_telemetry_observation_Diagnostics(offlineSession_GetDiagnostics());
					responseStatus = 200;
				}
				else if(strstr(commandString, "FixPartitionFilesErase"))
				{
					offlineSession_ClearDiagnostics();
					offlineSession_eraseAndRemountPartition();
					publish_debug_telemetry_observation_Diagnostics(offlineSession_GetDiagnostics());
					responseStatus = 200;
				}
				else if(strstr(commandString, "FixPartitionFilesCorrect"))
				{
					offlineSession_ClearDiagnostics();
					offlineSession_CheckAndCorrectFilesSystem();
					publish_debug_telemetry_observation_Diagnostics(offlineSession_GetDiagnostics());
					responseStatus = 200;
				}
				else if(strstr(commandString, "FixPartitionDiskCheck"))
				{
					fat_ClearDiagnostics();
					fat_CheckFilesSystem();
					publish_debug_telemetry_observation_Diagnostics(fat_GetDiagnostics());
					responseStatus = 200;
				}
				else if(strstr(commandString, "FixPartitionDiskErase"))
				{
					fat_ClearDiagnostics();
					fat_eraseAndRemountPartition();
					publish_debug_telemetry_observation_Diagnostics(fat_GetDiagnostics());
					responseStatus = 200;
				}
				else if(strstr(commandString, "listdirectory")){
					char * directory_path = index(commandString, '/');
					if(directory_path != NULL && strlen(directory_path) > 0){

						for(size_t i = strlen(directory_path)-1; i > 0; i--){
							if(isspace(directory_path[i]) != 0 || directory_path[i] == '\\' || directory_path[i] == ']'
								|| directory_path[i] == '"'){
								directory_path[i] = '\0';
							}
						}

						ESP_LOGI(TAG, "Listing directory: '%s'", directory_path);

						cJSON * result = cJSON_CreateObject();
						if(result == NULL){
							responseStatus = 500;
						}else{
							fat_list_directory(directory_path, result);
							char * result_str = cJSON_PrintUnformatted(result);
							cJSON_Delete(result);

							if(result_str != NULL){
								responseStatus = 200;
								publish_debug_telemetry_observation_Diagnostics(result_str);
								free(result_str);
							}else{
								responseStatus = 500;
							}
						}
					}else{
						ESP_LOGW(TAG, "listdirectory requested with missing path");
						responseStatus = 400;
					}
				}
				else
				{
					responseStatus = 400;
				}
			}
			else if(strstr(commandString, "GetFileDiagnostics"))
			{
				publish_debug_telemetry_observation_Diagnostics(offlineSession_GetDiagnostics());
				responseStatus = 200;
			}
			else if(strstr(commandString, "RunRCDTest"))
			{
				MessageType ret = MCU_SendCommandId(CommandRunRCDTest);
				if(ret == MsgCommandAck)
				{

					ESP_LOGW(TAG, "MCU RunRCDTest command OK");
					if(isMqttConnected())
					{
						publish_debug_telemetry_observation_Diagnostics("Cloud: RCD button test run");
					}
					responseStatus = 200;
				}
				else
				{
					ESP_LOGE(TAG, "MCU RunRCDTest command FAILED");
					responseStatus = 400;
				}
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
static bool isFirstConnection = true;
static int incrementalRefreshTimeout = 0;
static esp_err_t reconnectErr = ESP_OK;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{

	//ESP_LOGE(TAG, "<<<<receiving>>>> %d:%d: %.*s", event->data_len, event->topic_len, event->data_len, event->data);
	MqttSetRxDiagnostics(event->data_len, event->topic_len);

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

    	//Must reset to avoid repeated certificate checks
    	event->error_handle->esp_tls_stack_err = 0;
    	simulateTlsError = false;
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

		char message[50] = {0};
        // Request twin data on every startup, but not on every following reconnect
        if(isFirstConnection == true)
        {
			ridNr++;
			char devicetwin_topic[64];
			sprintf(devicetwin_topic, "$iothub/twin/GET/?$rid=%d", ridNr);
			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, NULL, 0, 1, 0);

			isFirstConnection = false;

			// Only show this event on first boot, not on every SAS token expiry with reconnect
			//publish_debug_message_event("Connected", cloud_event_level_information);
			sprintf(message, "Connected: %d", resetCounter);
        }
        else
        {
        	//publish_debug_message_event("Reconnected", cloud_event_level_information);
        	sprintf(message, "Reconnected: %d", resetCounter);
        }

        //publish_debug_message_event("Reconnected", cloud_event_level_information);
        publish_debug_message_event(message, cloud_event_level_information);

        resetCounter = 0;
        reconnectionAttempt = 0;

        mqttConnected = true;
        //When connected, reset connection timeout to default
        mqtt_config.reconnect_timeout_ms = 10000;
        esp_mqtt_set_config(mqtt_client, &mqtt_config);
        incrementalRefreshTimeout = 0;

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
        	cloudSettingsAreUpdated = true; //Ensure Current settings are transmitted at boot.
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

        	ESP_LOGW(TAG, "***** Data len: %d *****", event->data_len);

        	//Limit max size since we have not testet with large data size(many tags in package)
        	if((3000 > event->data_len) && (event->data_len > 10))
        	{
        		//Remove '\\' escape character due to uint8_t->char conversion
        		//char rfidList[event->data_len];
        		char * rfidList = calloc(event->data_len, 1);
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

        		free(rfidList);

        		ESP_LOGI(TAG, "***** AuthenticationListVersion: %d *****", version);

        		if(version >= 0)
        		{
        			//Set flag value to trig sending of AuthenticationListVersion from SessionHandler
        			rfidListIsUpdated = version;
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

			/*char responseBuffer[500]={0};//TODO: check length ~400
			BuildLocalSettingsResponse(responseBuffer);
			ESP_LOGW(TAG, "responseStringLength: %d, responseBuffer: %s", strlen(responseBuffer), responseBuffer);

			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, responseBuffer, 0, 1, 0);*/
			char * data = NULL;
			esp_mqtt_client_publish(mqtt_client, devicetwin_topic, data, 0, 1, 0);
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

        //memset(event->data, 0, event->data_len);

        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "About to connect, refreshing the token");

       	refresh_token(&mqtt_config);
		ESP_LOGD(TAG, "setting config with the new token");
        esp_mqtt_set_config(mqtt_client, &mqtt_config);
        reconnectionAttempt++;

        break;
    case MQTT_EVENT_ERROR:
    	resetCounter++;

    	ESP_LOGI(TAG, "MQTT_EVENT_ERROR: #%d, Error: %d %X", resetCounter, event->error_handle->esp_tls_stack_err, event->error_handle->esp_tls_stack_err);

    	if((network_WifiIsConnected() == true) || (LteIsConnected() == true))
    	{
    		bool slowBackoff = true;
    		if(chargeSession_IsCarConnected())
    			slowBackoff = false;

    		//As in Pro - use different limits if car is connected or not
    		double maxBackoff;
			if(slowBackoff) {
				maxBackoff = 180.0; // 3 min (+ 0-65 sec)
			}
			else {
				maxBackoff = 45.0; // 45 sec (+ 0-28 sec)
			}

			double backOffSeconds = powf(reconnectionAttempt, 2.0);

			if(backOffSeconds > maxBackoff)
				backOffSeconds = maxBackoff;

    		if(backOffSeconds <= maxBackoff)
    		{
				uint32_t rand = esp_random();
				float randomSeconds = rand/(0xffffffff * 1.0);

				if(slowBackoff)
					randomSeconds = (10.0 + backOffSeconds) * randomSeconds / 3.0;
				else
					randomSeconds = (10.0 + backOffSeconds) * randomSeconds / 2.0;


				ESP_LOGW(TAG, "*** Backoff 10 + %f + %f = %f (Rand %d)", backOffSeconds, randomSeconds, 10 + backOffSeconds + randomSeconds, rand);


				incrementalRefreshTimeout = 10 + backOffSeconds + randomSeconds;
    			//incrementalRefreshTimeout += 10000; // Increment refreshTimeout with 10 sec for every disconnected error as a backoff routine.
    			mqtt_config.reconnect_timeout_ms = (int)(incrementalRefreshTimeout * 1000);
    			esp_mqtt_set_config(mqtt_client, &mqtt_config);
    			ESP_LOGW(TAG, "*** Attempts: %d Refreshing timeout increased to %i (%i)***", reconnectionAttempt, incrementalRefreshTimeout, mqtt_config.reconnect_timeout_ms);
    		}
    		else
    		{
    			ESP_LOGW(TAG, "*** Reconnect at max: %i ***", incrementalRefreshTimeout);
    		}


			if((resetCounter == 10) || (resetCounter == 30) || (resetCounter == 60) || (resetCounter == 90))
			{
				reconnectErr = esp_mqtt_client_reconnect(mqtt_client);
				ESP_LOGI(TAG, "MQTT event reconnect! Error: %d", reconnectErr);
			}
    	}
    	else if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
    	{
    		if((resetCounter == 10) || (resetCounter == 30) || (resetCounter == 60) || (resetCounter == 90))
			{
				ESP_LOGI(TAG, "Refreshing Wifi UnConnected");
				network_updateWifi();
			}
    	}
    	else
    	{
    		ESP_LOGI(TAG, "No Wifi or LTE");
    	}

    	//Case if the Wifi router is not accessible. incrementalRefreshTimeout is not being changed
    	/*if((storage_Get_CommunicationMode() == eCONNECTION_WIFI) && (network_WifiIsConnected() == false))
    	{
    		ESP_LOGI(TAG, "Wifi not connected");

    		if(resetCounter >= 720) //With 10-sec timeout without increase 720 * 10 = 2 hours
			{
    			char buf[100]={0};
    			sprintf(buf, "#1 MQTT_EVENT_ERROR: network_WifiIsConnected() == false) 720 times. %d", reconnectErr);
    			storage_Set_And_Save_DiagnosticsLog(buf);

				ESP_LOGI(TAG, "MQTT_EVENT_ERROR restart");
				esp_restart();
			}
    	}
    	else
    	{
    		if(resetCounter >= 99) //With 10-sec timeout increase this is reached within 7420 sec (2+ hours)
    		{
    			char buf[100]={0};
    			sprintf(buf, "#2 MQTT_EVENT_ERROR: 99 times. %d", reconnectErr);
    			storage_Set_And_Save_DiagnosticsLog(buf);
    			ESP_LOGI(TAG, "MQTT_EVENT_ERROR restart");
				esp_restart();
    		}
		}*/

        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }

    memset(event->data, 0, event->data_len);
    event->data_len = 0;
    event->total_data_len = 0;

    return ESP_OK;
}

int cloud_listener_GetResetCounter()
{
	return resetCounter;
}


void cloud_listener_IncrementResetCounter()
{
	resetCounter++;
}

uint8_t* hex_decode(const char *in, size_t len,uint8_t *out)
{
        unsigned int i, t, hn, ln;

        for (t = 0,i = 0; i < len; i+=2,++t) {

                hn = in[i] > '9' ? in[i] - 'a' + 10 : in[i] - '0';
                ln = in[i+1] > '9' ? in[i+1] - 'a' + 10 : in[i+1] - '0';

                out[t] = (hn << 4 ) | ln;
        }

        return out;
}

void GetInstallationIdBase64(char * instId, char *encodedString)
{

	/// Remove the '-' to only have the GUID-numbers
	char instIdFormatted[37] = {0};
	int c;
	int nextValid = 0;
	for (c = 0; c < 36; c++)
	{
		if(instId[c] != '-')
		{
			instIdFormatted[nextValid] = instId[c];
			nextValid++;
		}
	}

	//ESP_LOGW(TAG,"%s -> %s", instId, instIdFormatted);

	uint8_t hex;
	char substring[3] = {0};
	uint8_t hexString[20] = {0};


	/// Must do special byte swapping due to the defined format of the GUID datatype.
	/// https://stackoverflow.com/questions/9195551/why-does-guid-tobytearray-order-the-bytes-the-way-it-does
	int i;
	int j = 0;
	for (i = 0; i < 16; i++)
	{
		memcpy(substring, &instIdFormatted[j], 2);

		/// Convert from char to bytes to save data as defined for Pro.
		hex_decode(substring, 2, &hex);

		if(i == 0)
			hexString[3] = hex;
		else if(i == 1)
		    hexString[2] = hex;
		else if(i == 2)
		    hexString[1] = hex;
		else if(i == 3)
		    hexString[0] = hex;

		else if(i == 4)
			hexString[5] = hex;
		else if(i == 5)
		    hexString[4] = hex;

		else if(i == 6)
			hexString[7] = hex;
		else if(i == 7)
		    hexString[6] = hex;

		else if(i == 8)
			hexString[8] = hex;
		else if(i == 9)
			hexString[9] = hex;

		else if(i >= 10)
			hexString[i] = hex;

		j +=2;
	}

	/// Base64 encode the installationId
	size_t token_len;
	size_t mac_len = 16; //!= 0 and modulo 4 == 0 (base64_encode gives error if not)
	char * encoded = base64_url_encode(hexString, mac_len, &token_len);

	strncpy(encodedString, encoded, 22);

	//ESP_LOGW(TAG,"InstallationId:  %s -> %s", instId, encodedString);
}

#define MQTT_KEEPALIVE_STANDALONE 1100
#define MQTT_KEEPALIVE_SYSTEM 300
void start_cloud_listener_task(struct DeviceInfo deviceInfo){

	cloudDeviceInfo = deviceInfo;

	ESP_LOGI(TAG, "Connecting to IotHub");

    static char broker_url[128] = {0};
    sprintf(broker_url, "mqtts://%s", MQTT_HOST);

    static char username[128];
    sprintf(username, "%s/%s/?api-version=2018-06-30", MQTT_HOST, cloudDeviceInfo.serialNumber);


    char * instId = storage_Get_InstallationId();
    int compare = strncmp(instId, INSTALLATION_ID, 36);


    if(compare != 0)
    {
    	//strcpy(instId, "a200c784-e914-491d-99ad-4e0fb5da229b"); // For testing
    	/// strcpy(instId, "00000000-0000-0000-0000-000000000000"); // For testing

    	char instIdEncoded[37] = {0};
    	GetInstallationIdBase64(instId, instIdEncoded);

    	sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), instIdEncoded);
    }
    else
    {
        sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, ROUTING_ID, INSTALLATION_ID_BASE64);
    }

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

    if(certificate_GetUsage())
    {
    	mqtt_config.use_global_ca_store = true;
    }
    else
    {
    	mqtt_config.use_global_ca_store = false;
    	ESP_LOGE(TAG, "*** CERTIFICATES NOT USED ***");
    }

    mqtt_config.transport = MQTT_TRANSPORT_OVER_SSL; //Should already be set in menuconfig, but set here to ensure.

    mqtt_config.lwt_qos = 1;
    mqtt_config.lwt_topic = event_topic;
    static char *lwt = "{\"EventType\":30,\"Message\":\"mqtt connection broke[lwt]\",\"Type\":5}";
    mqtt_config.lwt_msg = lwt;

    mqtt_config.disable_auto_reconnect = false;
    mqtt_config.reconnect_timeout_ms = 10000;

    //Max for Azure client is 1177: https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support
    //Ping is sent if no other communication has occured since timer.
    if(storage_Get_Standalone() == 0)
    	mqtt_config.keepalive = MQTT_KEEPALIVE_SYSTEM;		//180;//1100; //300;//120 is default;
    else
    	mqtt_config.keepalive = MQTT_KEEPALIVE_STANDALONE;

    //Don't use, causes disconnect and reconnect
    //mqtt_config.refresh_connection_after_ms = 20000;

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
    {

    	char instIdEncoded[37] = {0};
    	GetInstallationIdBase64(instId, instIdEncoded);
    	sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), instIdEncoded);
    }
    else
    {
        sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, ROUTING_ID, INSTALLATION_ID_BASE64);
    }

    ESP_LOGW(TAG,"New event_topic: %s ", event_topic);

    mqtt_config.lwt_topic = event_topic;

	esp_mqtt_set_config(mqtt_client, &mqtt_config);
}


void update_mqtt_event_pattern(bool usePingReply)
{
	ESP_LOGW(TAG," ### Setting PingReply to: %i ", usePingReply);

	char * instId = storage_Get_InstallationId();

    int compare = strncmp(instId, INSTALLATION_ID, 36);
    if(compare != 0)
    {

    	char instIdEncoded[37] = {0};
    	GetInstallationIdBase64(instId, instIdEncoded);
    	if(usePingReply)
    		sprintf(event_topic, MQTT_EVENT_PATTERN_PING_REPLY, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), instIdEncoded, "PR");
    	else
    		sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, storage_Get_RoutingId(), instIdEncoded);
    }
    else
    {
        sprintf(event_topic, MQTT_EVENT_PATTERN, cloudDeviceInfo.serialNumber, ROUTING_ID, INSTALLATION_ID_BASE64);
    }

    ESP_LOGW(TAG,"New event_topic: %s ", event_topic);

    mqtt_config.lwt_topic = event_topic;

	esp_mqtt_set_config(mqtt_client, &mqtt_config);
}


void periodic_refresh_token(uint8_t source)
{
	ESP_LOGW(TAG, "####### Periodic new token ######");

	esp_err_t err = esp_mqtt_client_stop(mqtt_client);

    if(err != ESP_OK)
    {
    	if(source == 2)
    	{
    		ESP_LOGI(TAG, "MQTT errorCounter refresh token failed restart: %d", err);
    		storage_Set_And_Save_DiagnosticsLog("#9 MQTT errorCounter refresh token failed");
    	}
    	else
    	{
    		ESP_LOGI(TAG, "MQTT periodic refresh token failed restart: %d", err);
    		storage_Set_And_Save_DiagnosticsLog("#6 MQTT periodic refresh token failed");
    	}

    	esp_restart();
    }
    else
    {
    	//Refreshes token in event: MQTT_EVENT_BEFORE_CONNECT
    	err = esp_mqtt_client_start(mqtt_client);
    	ESP_LOGI(TAG, "MQTT reconnect result: %d", err);
    }
}


/**
 * Change keepalive time to save data cost in standalone.
 * Takes it longer to detect broken connection and issue reconnect
 */
void cloud_listener_SetMQTTKeepAliveTime(uint8_t isStandalone)
{
	int previous = mqtt_config.keepalive;
    if(isStandalone == 0)
    	mqtt_config.keepalive = MQTT_KEEPALIVE_SYSTEM;		//180;//1100; //300;//120 is default;
    else
    	mqtt_config.keepalive = MQTT_KEEPALIVE_STANDALONE;

    ESP_LOGW(TAG, "Updated MQTT keepalive time: %d -> %d", previous, mqtt_config.keepalive);

    esp_mqtt_set_config(mqtt_client, &mqtt_config);
}
