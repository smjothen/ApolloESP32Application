#include "cJSON.h"
#include "esp_log.h"
#include "time.h"
#include <sys/time.h>
#include "stdio.h"
#include "esp_system.h"

#include "../../main/storage.h"
#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"
#include "../zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../i2c/include/i2cDevices.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "../cellular_modem/include/ppp_task.h"

#define TAG "OBSERVATIONS POSTER"

static bool startupMessage = true;

int publish_json(cJSON *payload){
    char *message = cJSON_PrintUnformatted(payload);

    if(message == NULL){
        ESP_LOGE(TAG, "failed to print json");
        cJSON_Delete(payload);
        return -2;
    }
    ESP_LOGI(TAG, "<<<sending>>> %s", message);

    int publish_err = publish_iothub_event(message);

    cJSON_Delete(payload);
    free(message);

    if(publish_err){
        ESP_LOGW(TAG, "publish to iothub failed");
        return -1;
    }
    return 0;
}

cJSON *create_observation(int observation_id, char *value){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    time_t now = tv_now.tv_sec;
    struct tm timeinfo = { 0 };
    char strftime_buf_head[64];
    char strftime_buf[128];
    time(&now);
    setenv("TZ", "UTC-0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf_head, sizeof(strftime_buf_head), "%Y-%m-%dT%H:%M:%S.", &timeinfo);
    snprintf(strftime_buf, sizeof(strftime_buf), "%s%03dZ", strftime_buf_head, (int)(tv_now.tv_usec/1000));
    
    cJSON_AddStringToObject(result, "ObservedAt", strftime_buf);
    cJSON_AddStringToObject(result, "Value", value);
    cJSON_AddNumberToObject(result, "ObservationId", (float) observation_id);
    cJSON_AddNumberToObject(result, "Type", (float) 1.0);

    return result;
}

cJSON *create_double_observation(int observation_id, double value){
    char value_string[32];
    sprintf(value_string, "%f", value);
    return create_observation(observation_id, value_string);
}

cJSON *create_uint32_t_observation(int observation_id, uint32_t value){
    char value_string[32];
    sprintf(value_string, "%d", value);
    return create_observation(observation_id, value_string);
}

cJSON *create_observation_collection(void){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    cJSON_AddArrayToObject(result, "Observations");
    cJSON_AddNumberToObject(result, "Type", (float) 6.0);
    return result;
}

int add_observation_to_collection(cJSON *collection, cJSON *observation){
    cJSON_AddItemToArray(
        cJSON_GetObjectItem(collection, "Observations"),
        observation
    );

    return 0;
}

/*int publish_debug_telemetry_observation(
    double temperature_5, double temperature_emeter, double rssi
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    //add_observation_to_collection(observations, create_observation(808, "debugstring1"));

    add_observation_to_collection(observations, create_double_observation(201, temperature_5));
    add_observation_to_collection(observations, create_double_observation(809, rssi));
    //add_observation_to_collection(observations, create_double_observation(202, temperature_emeter));

    return publish_json(observations);
}*/

int publish_debug_telemetry_observation_power(
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(501, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(502, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(503, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(507, current_l1));
    add_observation_to_collection(observations, create_double_observation(508, current_l2));
    add_observation_to_collection(observations, create_double_observation(509, current_l2));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_cloud_settings()
{
    ESP_LOGD(TAG, "sending cloud settings");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_uint32_t_observation(AuthenticationRequired, storage_Get_AuthenticationRequired()));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentInMaximum, storage_Get_CurrentInMaximum()));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentInMinimum, storage_Get_CurrentInMinimum()));

    add_observation_to_collection(observations, create_uint32_t_observation(MaxPhases, (uint32_t)storage_Get_MaxPhases()));
    add_observation_to_collection(observations, create_uint32_t_observation(ChargerOfflinePhase, (uint32_t)storage_Get_DefaultOfflinePhase()));
    add_observation_to_collection(observations, create_double_observation(ChargerOfflineCurrent, storage_Get_DefaultOfflineCurrent()));

    add_observation_to_collection(observations, create_uint32_t_observation(ParamIsEnabled, (uint32_t)storage_Get_IsEnabled()));
    add_observation_to_collection(observations, create_observation(InstallationId, storage_Get_InstallationId()));
    add_observation_to_collection(observations, create_observation(RoutingId, storage_Get_RoutingId()));
    add_observation_to_collection(observations, create_observation(ChargePointName, storage_Get_ChargerName()));

    add_observation_to_collection(observations, create_uint32_t_observation(DiagnosticsMode, storage_Get_DiagnosticsMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)storage_Get_Standalone()));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_local_settings()
{
    ESP_LOGD(TAG, "sending local settings");

    cJSON *observations = create_observation_collection();

    if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
    	add_observation_to_collection(observations, create_observation(CommunicationMode, "Wifi"));
    else if (storage_Get_CommunicationMode() == eCONNECTION_LTE)
    	add_observation_to_collection(observations, create_observation(CommunicationMode, "LTE"));

    add_observation_to_collection(observations, create_uint32_t_observation(ParamNetworkType, storage_Get_NetworkType()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, storage_Get_Standalone()));
    add_observation_to_collection(observations, create_double_observation(StandAloneCurrent, storage_Get_StandaloneCurrent()));
    add_observation_to_collection(observations, create_double_observation(ChargerOfflineCurrent, storage_Get_DefaultOfflineCurrent()));
    //add_observation_to_collection(observations, create_uint32_t_observation(ChargerOfflinePhase, storage_Get_DefaultOfflinePhase()));
    add_observation_to_collection(observations, create_double_observation(HmiBrightness, storage_Get_HmiBrightness()));

    return publish_json(observations);
}

int publish_debug_telemetry_observation_NFC_tag_id(char * NFCHexString)
{
    ESP_LOGD(TAG, "sending NFC telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(ChargerCurrentUserUuid, NFCHexString));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_CompletedSession(char * CompletedSessionString)
{
    ESP_LOGD(TAG, "sending CompletedSession");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(CompletedSession, CompletedSessionString));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_StartUpParameters()
{
    ESP_LOGD(TAG, "sending startup telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_uint32_t_observation(AuthenticationRequired, (uint32_t)storage_Get_AuthenticationRequired()));
	add_observation_to_collection(observations, create_uint32_t_observation(MaxPhases, (uint32_t)storage_Get_MaxPhases()));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamIsEnabled, (uint32_t)storage_Get_IsEnabled()));

    //add_observation_to_collection(observations, create_observation(802, "Apollo5"));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)storage_Get_Standalone()));

    add_observation_to_collection(observations, create_observation(ParamSmartComputerAppVersion, GetSoftwareVersion()));
    add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource,  MCU_GetResetSource()));
    add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource,  esp_reset_reason()));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_WifiParameters()
{
    ESP_LOGD(TAG, "sending LTE telemetry");

    cJSON *observations = create_observation_collection();

    char wifiMAC[18] = {0};
    esp_read_mac((uint8_t*)wifiMAC, 0); //0=Wifi station
    sprintf(wifiMAC, "%02x:%02x:%02x:%02x:%02x:%02x", wifiMAC[0],wifiMAC[1],wifiMAC[2],wifiMAC[3],wifiMAC[4],wifiMAC[5]);

    add_observation_to_collection(observations, create_observation(MacWiFi, wifiMAC));

    return publish_json(observations);
}

int publish_debug_telemetry_observation_LteParameters()
{
    ESP_LOGD(TAG, "sending LTE telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(LteImsi, LTEGetImsi()));
    add_observation_to_collection(observations, create_observation(LteMsisdn, "0"));
    add_observation_to_collection(observations, create_observation(LteIccid, LTEGetIccid()));
    add_observation_to_collection(observations, create_observation(LteImei, LTEGetImei()));

    return publish_json(observations);
}



static uint32_t txCnt = 0;

int publish_debug_telemetry_observation_all(
	double temperature_emeter1, double temperature_emeter2, double temperature_emeter3,
	double temperature_TM, double temperature_TM2,
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3,
	double rssi
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperature, I2CGetSHT30Temperature()));
    add_observation_to_collection(observations, create_double_observation(ParamHumidity, I2CGetSHT30Humidity()));

    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter, temperature_emeter1));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter2, temperature_emeter2));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter3, temperature_emeter3));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT, temperature_TM));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT2, temperature_TM2));

    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase1, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase2, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase3, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, current_l1));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, current_l2));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, current_l3));

    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, MCU_GetPower()));
    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePowerSession, MCU_GetEnergy()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeMode, (uint32_t)MCU_GetchargeMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));

	add_observation_to_collection(observations, create_double_observation(CommunicationSignalStrength, rssi));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, (uint32_t)MCU_GetWarnings()));

	if(startupMessage == true)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource, (uint32_t)MCU_GetResetSource()));
		add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource, (uint32_t)esp_reset_reason()));

		startupMessage = false;
	}

	txCnt++;
	char buf[256];
	sprintf(buf, "#%d SHT: %3.2f %3.1f%%  T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  SW: %d  DBC: %d", txCnt, I2CGetSHT30Temperature(), I2CGetSHT30Humidity(), temperature_emeter1, temperature_emeter2, temperature_emeter3, temperature_TM, temperature_TM2, voltage_l1, voltage_l2, voltage_l3, current_l1, current_l2, current_l3, MCU_GetSwitchState(), MCU_GetDebugCounter());
	//sprintf(buf, "#%d SHT: %3.2f %3.1f%%  T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f ", txCnt, I2CGetSHT30Temperature(), I2CGetSHT30Humidity(), temperature_emeter1, temperature_emeter2, temperature_emeter3, temperature_TM, temperature_TM2, voltage_l1, voltage_l2, voltage_l3, current_l1, current_l2, current_l3);
	add_observation_to_collection(observations, create_observation(808, buf));

	int ret = publish_json(observations);

	//cJSON_Delete(observations);

    return ret;//publish_json(observations);
}


int publish_uint32_observation(int observationId, uint32_t value){
    return publish_json(create_uint32_t_observation(observationId, value));
}

int publish_double_observation(int observationId, double value){
    return publish_json(create_double_observation(observationId, value));
}

int publish_string_observation(int observationId, char *message){
    return publish_json(create_observation(observationId, message));
}

int publish_diagnostics_observation(char *message){
    return publish_json(create_observation(808, message));
}

int publish_debug_message_event(char *message, cloud_event_level level){

    cJSON *event = cJSON_CreateObject();
    if(event == NULL){return -10;}

    cJSON_AddNumberToObject(event, "EventType", level);
    cJSON_AddStringToObject(event, "Message", message);
    cJSON_AddNumberToObject(event, "Type", (float) 5.0);

    return publish_json(event);
}

int publish_cloud_pulse(void){
    cJSON *pulse = cJSON_CreateObject();
    if(pulse == NULL){return -10;}

    cJSON_AddNumberToObject(pulse, "Type", (float) 2.0);

    ESP_LOGD(TAG, "sending pulse to cloud");
    return publish_json(pulse);
}

int publish_noise(void){
    int events_to_send = 10;

    cJSON *event = cJSON_CreateObject();
    if(event == NULL){return -10;}

    cJSON_AddNumberToObject(event, "EventType", cloud_event_level_information);
    cJSON_AddStringToObject(event, "Message", "Noisy message to stress test the system.");
    cJSON_AddNumberToObject(event, "Type", (float) 5.0);

    char *message = cJSON_PrintUnformatted(event);
    
    ESP_LOGI(TAG, "sending %d stress test events", events_to_send);
    for(int i = 0; i<events_to_send; i++){
        int publish_err = publish_iothub_event(message);
        if(publish_err<0){
            ESP_LOGE(TAG, "Publish error in stress test message %d", i);
        }
    }

    ESP_LOGI(TAG, "sent stress test events.");
        
    cJSON_Delete(event);
    free(message);
    return 0;
}
