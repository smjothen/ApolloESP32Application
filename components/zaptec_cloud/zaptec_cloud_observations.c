#include "cJSON.h"
#include "esp_log.h"
#include "time.h"
#include <sys/time.h>
#include "stdio.h"
#include "esp_system.h"
#include "string.h"
#include "esp_ota_ops.h"

#include "../../main/storage.h"
#include "../../main/main.h"
#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"
#include "../zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../i2c/include/i2cDevices.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "../cellular_modem/include/ppp_task.h"
#include "../../main/chargeSession.h"
#include "../apollo_ota/include/apollo_ota.h"
#include "../apollo_ota/include/pic_update.h"
#include "../../main/certificate.h"

#define TAG "OBSERVATIONS POSTER"

static bool startupMessage = true;

int _publish_json(cJSON *payload, bool blocking, TickType_t xTicksToWait){
    char *message = cJSON_PrintUnformatted(payload);

    if(message == NULL){
        ESP_LOGE(TAG, "failed to print json");
        cJSON_Delete(payload);
        return -2;
    }
    ESP_LOGI(TAG, "<<<sending>>> %s", message);

    int publish_err;
    if(blocking){
        publish_err = publish_iothub_event_blocked(message, xTicksToWait);
    }else{
        publish_err = publish_iothub_event(message);
    }

    cJSON_Delete(payload);
    free(message);

    if(publish_err<0){
        ESP_LOGW(TAG, "publish to iothub failed");
        return -1;
    }
    return 0;
}

int publish_json(cJSON *payload){
    return _publish_json(payload, false, 0);
}

int publish_json_blocked(cJSON *payload, int timeout_ms){
    return _publish_json(payload, true, pdMS_TO_TICKS(timeout_ms));
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
    sprintf(value_string, "%.3f", value);
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

    uint8_t networkType = storage_Get_NetworkType();
    if(networkType != 0)
    	add_observation_to_collection(observations, create_uint32_t_observation(ParamNetworkType, (uint32_t)networkType));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)storage_Get_Standalone()));
    add_observation_to_collection(observations, create_double_observation(StandAloneCurrent, MCU_StandAloneCurrent()));
    add_observation_to_collection(observations, create_double_observation(ChargerOfflineCurrent, storage_Get_DefaultOfflineCurrent()));
    //add_observation_to_collection(observations, create_uint32_t_observation(ChargerOfflinePhase, storage_Get_DefaultOfflinePhase()));
    add_observation_to_collection(observations, create_uint32_t_observation(PermanentCableLock, (uint32_t)storage_Get_PermanentLock()));
    add_observation_to_collection(observations, create_double_observation(HmiBrightness, storage_Get_HmiBrightness()));

    return publish_json(observations);
}


/*int publish_telemetry_observation_CurrentCommandFeedback()
{
    ESP_LOGD(TAG, "sending local settings");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamChargeCurrentUserMax, storage_Get_DefaultOfflineCurrent()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamSetPhases, (uint32_t)HOLD_GetSetPhases()));


    return publish_json(observations);
}*/

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


int publish_debug_telemetry_observation_GridTestResults(char * gridTestResults)
{
    ESP_LOGD(TAG, "sending GridTestResults");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(GridTestResult, gridTestResults));

    return publish_json(observations);
}


/*
 * Returns the last set of values set. If the MCU values for any reason has been cleared, these are a backup of last
 */
int publish_debug_telemetry_observation_InstallationConfigOnFile()
{
    ESP_LOGD(TAG, "sending GridTestResults");

    cJSON *observations = create_observation_collection();

    char buf[100];
    sprintf(buf, "On file: MaxInstallationCurrentConfig: %f, StandaloneCurrent: %f, PhaseRotation %d", storage_Get_MaxInstallationCurrentConfig(), storage_Get_StandaloneCurrent(), storage_Get_PhaseRotation());

    ESP_LOGI(TAG, "Sending InstallationConfigOnFile telemetry: %d/100", strlen(buf));
    add_observation_to_collection(observations, create_observation(808, buf));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_StartUpParameters()
{
    ESP_LOGD(TAG, "sending startup telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(InstallationId, storage_Get_InstallationId()));
    add_observation_to_collection(observations, create_observation(RoutingId, storage_Get_RoutingId()));

    add_observation_to_collection(observations, create_double_observation(ParamChargeCurrentUserMax, MCU_GetChargeCurrentUserMax()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamSetPhases, (uint32_t)HOLD_GetSetPhases()));

    add_observation_to_collection(observations, create_observation(SessionIdentifier, chargeSession_GetSessionId()));

    add_observation_to_collection(observations, create_uint32_t_observation(AuthenticationRequired, (uint32_t)storage_Get_AuthenticationRequired()));
	add_observation_to_collection(observations, create_uint32_t_observation(MaxPhases, (uint32_t)storage_Get_MaxPhases()));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamIsEnabled, (uint32_t)storage_Get_IsEnabled()));

    /*These are sent at startup with localsettings.
    if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
    	add_observation_to_collection(observations, create_observation(CommunicationMode, "Wifi"));
    else if (storage_Get_CommunicationMode() == eCONNECTION_LTE)
    	add_observation_to_collection(observations, create_observation(CommunicationMode, "LTE"));
    */

    //add_observation_to_collection(observations, create_observation(802, "Apollo5"));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)storage_Get_Standalone()));

    add_observation_to_collection(observations, create_observation(ParamSmartComputerAppVersion, GetSoftwareVersion()));
    add_observation_to_collection(observations, create_observation(ParamSmartMainboardAppSwVersion, MCU_GetSwVersionString()));
    add_observation_to_collection(observations, create_observation(SourceVersion, esp_ota_get_app_description()->version));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamSmartMainboardBootSwVersion, (uint32_t)get_bootloader_version()));
    add_observation_to_collection(observations, create_uint32_t_observation(CertificateVersion, (uint32_t)certificate_GetCurrentBundleVersion()));

    add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource,  MCU_GetResetSource()));
    add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource,  esp_reset_reason()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, (uint32_t)MCU_GetWarnings()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeMode, (uint32_t)MCU_GetchargeMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(PhaseRotation, (uint32_t)storage_Get_PhaseRotation()));
    add_observation_to_collection(observations, create_observation(ChargePointName, storage_Get_ChargerName()));

    char buf[256];
    GetTimeOnString(buf);
    sprintf(buf + strlen(buf), " Boot: ESP: v%s, MCU: v%s  Switch: %d/MaxInst: %2.1fA Sta: %2.1fA  ChargeState: %d  MCnt: %d  Partition: %s", GetSoftwareVersion(), MCU_GetSwVersionString(), MCU_GetSwitchState(), MCU_ChargeCurrentInstallationMaxLimit(), MCU_StandAloneCurrent(), MCU_GetChargeOperatingMode(), MCU_GetDebugCounter(), OTAReadRunningPartition());

    ESP_LOGI(TAG, "Sending charging telemetry: %d/256", strlen(buf));
    add_observation_to_collection(observations, create_observation(808, buf));


    return publish_json(observations);
}

int publish_debug_telemetry_observation_ChargingStateParameters()
{
    ESP_LOGD(TAG, "sending ChargeState telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeMode, (uint32_t)MCU_GetchargeMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));

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
    //add_observation_to_collection(observations, create_observation(LteMsisdn, "0"));
    add_observation_to_collection(observations, create_observation(LteIccid, LTEGetIccid()));
    add_observation_to_collection(observations, create_observation(LteImei, LTEGetImei()));

    return publish_json(observations);
}



static uint32_t txCnt = 0;

int publish_debug_telemetry_observation_all(double rssi){
    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperature, I2CGetSHT30Temperature()));
    add_observation_to_collection(observations, create_double_observation(ParamHumidity, I2CGetSHT30Humidity()));

    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter, MCU_GetEmeterTemperature(0)));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter2, MCU_GetEmeterTemperature(1)));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter3, MCU_GetEmeterTemperature(2)));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT, MCU_GetTemperaturePowerBoard(0)));
    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT2, MCU_GetTemperaturePowerBoard(1)));

    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase1, MCU_GetVoltages(0)));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase2, MCU_GetVoltages(1)));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase3, MCU_GetVoltages(2)));

    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, MCU_GetCurrents(0)));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, MCU_GetCurrents(1)));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, MCU_GetCurrents(2)));

    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, MCU_GetPower()));
    //add_observation_to_collection(observations, create_double_observation(ParamTotalChargePowerSession, MCU_GetEnergy()));
    //add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeMode, (uint32_t)MCU_GetchargeMode()));
    //add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));

	add_observation_to_collection(observations, create_double_observation(CommunicationSignalStrength, rssi));
	//add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, (uint32_t)MCU_GetWarnings()));

	if(startupMessage == true)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource, (uint32_t)MCU_GetResetSource()));
		add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource, (uint32_t)esp_reset_reason()));

		startupMessage = false;
	}

	txCnt++;
	char buf[256];
	GetTimeOnString(buf);
	sprintf(buf + strlen(buf), " SHT: %3.2f %3.1f%%  T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  SW: %d  MCnt: %d", I2CGetSHT30Temperature(), I2CGetSHT30Humidity(), MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2), MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1), MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2), MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2), MCU_GetSwitchState(), MCU_GetDebugCounter());

	if(storage_Get_DiagnosticsMode() == eNFC_ERROR_COUNT)
	{
		sprintf(buf + strlen(buf), " NFC Pass: %d Fail: %d ", GetPassedDetectedCounter(), GetFailedDetectedCounter());
	}

	ESP_LOGI(TAG, "Sending charging telemetry: %d/256", strlen(buf));

	add_observation_to_collection(observations, create_observation(808, buf));

	int ret = publish_json(observations);

	//cJSON_Delete(observations);

    return ret;//publish_json(observations);
}



static uint32_t previousWarnings = 0;
static uint32_t previousNotifications = 0;
static uint8_t previousNetworkType = 0xff;
static float previousChargeCurrentUserMax = 0.0;
static int previousSetPhases = 0;
static uint8_t previousPhaseRotation = 0;
static uint8_t previousChargeMode = 0;
static uint8_t previousChargeOperatingMode = 0;
static uint8_t previousIsStandalone = 0xff;
static float previousStandaloneCurrent = -1.0;
static float previousMaxInstallationCurrentConfig = -1.0;
static uint8_t previousSwitchState = 0xff;
static uint8_t previousPermanentLock = 0xff;
static uint8_t previousCableType = 0xff;
static float previousPower = -1.0;
static float previousEnergy = -1.0;
static uint32_t previousDiagnosticsMode = 0;

int publish_telemetry_observation_on_change(){
    ESP_LOGD(TAG, "sending on change telemetry");

    bool isChange = false;

    cJSON *observations = create_observation_collection();


    /*add_observation_to_collection(observations, create_double_observation(ParamInternalTemperature, I2CGetSHT30Temperature()));
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

	add_observation_to_collection(observations, create_double_observation(CommunicationSignalStrength, rssi));*/

    uint8_t chargeMode = MCU_GetchargeMode();
	if ((previousChargeMode != chargeMode) && (chargeMode != 0) && (chargeMode != 0xff))
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeMode, (uint32_t)chargeMode));
		previousChargeMode = chargeMode;
		isChange = true;
	}

	uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();
	if ((previousChargeOperatingMode != chargeOperatingMode) && (chargeOperatingMode != 0))
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)chargeOperatingMode));
		previousChargeOperatingMode = chargeOperatingMode;
		isChange = true;
	}

    float chargeCurrentUserMax = MCU_GetChargeCurrentUserMax();
    int setPhases = HOLD_GetSetPhases();
	if ((previousChargeCurrentUserMax != chargeCurrentUserMax) || (previousSetPhases != setPhases))
	{
		add_observation_to_collection(observations, create_double_observation(ParamChargeCurrentUserMax, chargeCurrentUserMax));
		add_observation_to_collection(observations, create_uint32_t_observation(ParamSetPhases, (uint32_t)setPhases));
		previousChargeCurrentUserMax = chargeCurrentUserMax;
		previousSetPhases = setPhases;
		isChange = true;
		ESP_LOGW(TAG, "CC ACK: User current %.2f, %d", chargeCurrentUserMax, setPhases);
	}

    uint8_t networkType = MCU_GetGridType();
    if ((previousNetworkType != networkType))// && (networkType != 0))
    {
    	add_observation_to_collection(observations, create_uint32_t_observation(ParamNetworkType, (uint32_t)networkType));
    	previousNetworkType = networkType;
    	isChange = true;
    }

    uint32_t warnings = MCU_GetWarnings();
    if(previousWarnings != warnings)
    {
    	add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, warnings));
    	previousWarnings = warnings;
    	isChange = true;
    }

    uint32_t notifications = GetCombinedNotifications();
    if(previousNotifications != notifications)
    {
    	add_observation_to_collection(observations, create_uint32_t_observation(Notifications, notifications));
    	previousNotifications = notifications;
    	isChange = true;
    }

    uint32_t phaseRotation = storage_Get_PhaseRotation();
	if(previousPhaseRotation != phaseRotation)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(PhaseRotation, phaseRotation));
		previousPhaseRotation = phaseRotation;
		isChange = true;
	}

	uint8_t isStandalone = storage_Get_Standalone();
	if((previousIsStandalone != isStandalone) && (isStandalone != 0xff))
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)isStandalone));
		previousIsStandalone = isStandalone;
		isChange = true;
	}

	float standaloneCurrent = MCU_StandAloneCurrent();//storage_Get_StandaloneCurrent();
	if((previousStandaloneCurrent != standaloneCurrent))
	{
		add_observation_to_collection(observations, create_double_observation(StandAloneCurrent, standaloneCurrent));
		previousStandaloneCurrent = standaloneCurrent;
		isChange = true;
	}

	float maxInstallationCurrentConfig = MCU_ChargeCurrentInstallationMaxLimit();//storage_Get_MaxInstallationCurrentConfig();
	if((previousMaxInstallationCurrentConfig != maxInstallationCurrentConfig))
	{
		add_observation_to_collection(observations, create_double_observation(ChargeCurrentInstallationMaxLimit, maxInstallationCurrentConfig));
		previousMaxInstallationCurrentConfig = maxInstallationCurrentConfig;
		isChange = true;
	}

	uint8_t switchState = MCU_GetSwitchState();
	if((previousSwitchState != switchState) && (switchState != 0xff))
	{
		add_observation_to_collection(observations, create_uint32_t_observation(SwitchPosition, (uint32_t)switchState));
		previousSwitchState = switchState;
		isChange = true;
	}

	uint8_t permanentLock = storage_Get_PermanentLock();
	if(previousPermanentLock != permanentLock)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(PermanentCableLock, (uint32_t)permanentLock));
		previousPermanentLock = permanentLock;
		isChange = true;
	}

	uint8_t cableType = MCU_GetCableType();
	if(previousCableType != cableType)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ParamCableType, (uint32_t)cableType));
		previousCableType = cableType;
		isChange = true;
	}

	float power = MCU_GetPower();
	if((power > previousPower + 500) != (power < (previousPower - 500))) //500W
	{
		if(power < 0.0)
			power = 0.0;

		add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, power));

		//When power changes also update currents to be responsive
		add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, MCU_GetCurrents(0)));
		add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, MCU_GetCurrents(1)));
		add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, MCU_GetCurrents(2)));
		previousPower = power;
		isChange = true;
	}

	float energy = chargeSession_Get().Energy;
	if((energy > previousEnergy + 0.1) != (energy < (previousEnergy - 0.1))) //0.1kWh
	{
		if(energy < 0.0)
			energy = 0.0;
		add_observation_to_collection(observations, create_double_observation(ParamTotalChargePowerSession, energy));
		previousEnergy = energy;
		isChange = true;
	}


	uint32_t diagnosticsMode = storage_Get_DiagnosticsMode();
	if(previousDiagnosticsMode != diagnosticsMode)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(DiagnosticsMode, diagnosticsMode));
		previousDiagnosticsMode = diagnosticsMode;
		isChange = true;
	}

	//Check ret and retry?
    int ret = 0;

    if(isChange == true)
    	ret = publish_json(observations);
    else
    	cJSON_Delete(observations);

    return ret;
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

int publish_prodtest_line(char *message){
    return publish_json_blocked(create_observation(ProductionTestResults, message), 10*1000);
}
