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
#include "../i2c/include/RTC.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "../cellular_modem/include/ppp_task.h"
#include "../../main/chargeSession.h"
#include "../apollo_ota/include/apollo_ota.h"
#include "../apollo_ota/include/pic_update.h"
#include "../../main/certificate.h"
#include "sessionHandler.h"
#include "../components/adc/adc_control.h"
#include "../components/ble/ble_service_wifi_config.h"
#include "../../main/connectivity.h"
#include "offlineHandler.h"
#include "chargeController.h"
#include "mqtt_client.h"
#include <math.h>

#include "../../main/IT3PCalculator.h"

static const char *TAG = "OBSERV POSTER  ";

struct MqttDataDiagnostics mqttDiagnostics = {0};


void MqttSetRxDiagnostics(uint32_t bytes, uint32_t metabytes)
{
	mqttDiagnostics.mqttRxBytes += bytes;
	mqttDiagnostics.mqttRxBytesIncMeta += (bytes + metabytes);
	mqttDiagnostics.nrOfRxMessages++;
}


struct MqttDataDiagnostics MqttGetDiagnostics()
{
	return mqttDiagnostics;
}

void MqttDataReset()
{
	memset(&mqttDiagnostics, 0, sizeof(mqttDiagnostics));
}

int _publish_json(cJSON *payload, bool blocking, TickType_t xTicksToWait){
    char *message = cJSON_PrintUnformatted(payload);

    if(message == NULL){
        ESP_LOGE(TAG, "failed to print json");
        cJSON_Delete(payload);
        return -2;
    }

    int len = strlen(message);
    //ESP_LOGE(TAG, "<<<sending>>> %d: %s", len, message);

    int publish_err;
    if(blocking){
        publish_err = publish_iothub_event_blocked(message, xTicksToWait);
    }else{
        publish_err = publish_iothub_event(message);
    }

    cJSON_Delete(payload);
    free(message);

    if(publish_err == 0)
    {
    	mqttDiagnostics.mqttTxBytes += len;
    	mqttDiagnostics.mqttTxBytesIncMeta += (len+112);
    	mqttDiagnostics.nrOfTxMessages++;
    }

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


static bool initiateHoldRequestTimeStamp = false;
void InitiateHoldRequestTimeStamp()
{
	initiateHoldRequestTimeStamp = true;
}


/*
 * This function returns the format required for CompletedSession
 */
void GetUTCTimeString(char * timeString, time_t *epochSec, uint32_t *epochUsec)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	char strftime_buf[64] = {0};

	time(&now);

	setenv("TZ", "UTC-0", 1);
	tzset();

	localtime_r(&now, &timeinfo);

	struct timeval t_now;
	gettimeofday(&t_now, NULL);

	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%02m-%02dT%02H:%02M:%02S", &timeinfo);

	sprintf(strftime_buf+strlen(strftime_buf), ".%06dZ", (uint32_t)t_now.tv_usec);
	strcpy(timeString, strftime_buf);
	if((epochSec == NULL) || (epochSec == NULL))
		return;

	*epochSec = now;
	*epochUsec = (uint32_t)t_now.tv_usec;
}


static struct HoldSessionStartTime timeStruct = {0};

struct HoldSessionStartTime *cloud_observation_GetTimeStruct()
{
	return &timeStruct;
}

void cloud_observation_SetTimeStruct(char * _timeString, time_t _holdEpochSec, uint32_t _holdEpochUsec, bool _usedInSession)
{
	strcpy(timeStruct.timeString, _timeString);
	timeStruct.holdEpochSec = _holdEpochSec;
	timeStruct.holdEpochUsec = _holdEpochUsec;
	timeStruct.usedInSession = _usedInSession;
}

void cloud_observation_ClearTimeStruct()
{
	memset(&timeStruct, 0 , sizeof(timeStruct));
}



cJSON *create_observation(int observation_id, char *value){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    char strftime_buf[32];

    /// When a new request command is sent the first time, hold the timestamp to use as StartDateTime in the CompletedSessionStructure.
    if((observation_id == 710) && (initiateHoldRequestTimeStamp == true) && (timeStruct.usedInSession == false) && (timeStruct.usedInRequest == false))
    {
    	//Get the time-string for Observation AND CompletedSession use
    	GetUTCTimeString(timeStruct.timeString, &timeStruct.holdEpochSec, &timeStruct.holdEpochUsec);

    	timeStruct.usedInSession = false;
    	timeStruct.usedInRequest = true;

    	initiateHoldRequestTimeStamp = false;

    	cJSON_AddStringToObject(result, "ObservedAt", timeStruct.timeString);

    	ESP_LOGW(TAG, "Made REQUESTING TimeStamp: %i: %s for use in Session", strlen(timeStruct.timeString), timeStruct.timeString);
    }
    //Start time was defined in ChargeSession_Set_StartTime(), use for first requesting message
    else if((observation_id == 710) && (initiateHoldRequestTimeStamp == true) && (timeStruct.usedInSession == true) && (timeStruct.usedInRequest == false))
    {
    	cJSON_AddStringToObject(result, "ObservedAt", timeStruct.timeString);
    	initiateHoldRequestTimeStamp = false;
    	timeStruct.usedInRequest = true;

    	ESP_LOGW(TAG, "Using SESSION TimeStamp: %i: %s in REQUEST", strlen(timeStruct.timeString), timeStruct.timeString);
    }
    else

    {
    	/// Get the time-string for Observation only use
    	GetUTCTimeString(strftime_buf, NULL, NULL);
    	cJSON_AddStringToObject(result, "ObservedAt", strftime_buf);
    }

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

cJSON *create_int32_t_observation(int observation_id, int32_t value){
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

int publish_debug_telemetry_observation_power(){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, MCU_GetCurrents(0)));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, MCU_GetCurrents(1)));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, MCU_GetCurrents(2)));

    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase1, MCU_GetVoltages(0)));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase2, MCU_GetVoltages(1)));
    add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase3, MCU_GetVoltages(2)));

    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, MCU_GetPower()));
    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePowerSession, chargeSession_Get().Energy));

    if(IsUKOPENPowerBoardRevision())
    {
    	add_observation_to_collection(observations, create_double_observation(ParamOPENVoltage, MCU_GetOPENVoltage()));
    }

    return publish_json(observations);
}


int publish_debug_telemetry_observation_cloud_settings()
{
    ESP_LOGD(TAG, "sending cloud settings");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_uint32_t_observation(AuthenticationRequired, storage_Get_AuthenticationRequired()));
    add_observation_to_collection(observations, create_uint32_t_observation(NrOfChargeCards, storage_ReadNrOfTagsOnFile()));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentInMaximum, storage_Get_CurrentInMaximum()));
    add_observation_to_collection(observations, create_double_observation(ParamCurrentInMinimum, storage_Get_CurrentInMinimum()));

    add_observation_to_collection(observations, create_uint32_t_observation(MaxPhases, (uint32_t)GetMaxPhases()));
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

    //uint8_t networkType = storage_Get_NetworkType();
    //if(networkType != 0)
    	//add_observation_to_collection(observations, create_uint32_t_observation(ParamNetworkType, (uint32_t)networkType));
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


int publish_debug_telemetry_observation_AddNewChargeCard(char * NewChargeCardString)
{
    ESP_LOGD(TAG, "sending AddNewChargeCard telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(NewChargeCard, NewChargeCardString));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_CompletedSession(char * CompletedSessionString)
{
    ESP_LOGD(TAG, "sending CompletedSession");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(CompletedSession, CompletedSessionString));

    return publish_json_blocked(observations, 10000);
}


int publish_debug_telemetry_observation_GridTestResults(char * gridTestResults)
{
    ESP_LOGD(TAG, "sending GridTestResults");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(GridTestResult, gridTestResults));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_Diagnostics(char * diagnostics)
{
    ESP_LOGD(TAG, "sending diagnostics");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(ParamDiagnosticsString, diagnostics));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_DiagnosticsLog()
{
    ESP_LOGD(TAG, "sending diagnosticsLog");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(InternalDiagnosticsLog, storage_Get_DiagnosticsLog()));

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
    snprintf(buf, sizeof(buf), "On file: MaxInstallationCurrentConfig: %f, StandaloneCurrent: %f, PhaseRotation %d", storage_Get_MaxInstallationCurrentConfig(), storage_Get_StandaloneCurrent(), storage_Get_PhaseRotation());

    ESP_LOGI(TAG, "Sending InstallationConfigOnFile telemetry: %d/100", strlen(buf));
    add_observation_to_collection(observations, create_observation(808, buf));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_StartUpParameters()
{
    ESP_LOGD(TAG, "sending startup telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamChargeCurrentUserMax, MCU_GetChargeCurrentUserMax()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamSetPhases, (uint32_t)HOLD_GetSetPhases()));
    add_observation_to_collection(observations, create_observation(SessionIdentifier, chargeSession_GetSessionId()));
	add_observation_to_collection(observations, create_uint32_t_observation(ParamIsStandalone, (uint32_t)storage_Get_Standalone()));

	add_observation_to_collection(observations, create_observation(ParamSmartComputerAppVersion, GetSoftwareVersion()));
    add_observation_to_collection(observations, create_observation(ParamSmartMainboardAppSwVersion, MCU_GetSwVersionString()));
#ifdef DEVELOPEMENT_URL
    char sourceVersionString[38] = {0};
    snprintf(sourceVersionString, 38, "%s (DEV)",(char*)esp_ota_get_app_description()->version);
    add_observation_to_collection(observations, create_observation(SourceVersion, sourceVersionString));
#else
    add_observation_to_collection(observations, create_observation(SourceVersion, (char*)esp_ota_get_app_description()->version));
#endif
    add_observation_to_collection(observations, create_uint32_t_observation(ParamSmartMainboardBootSwVersion, (uint32_t)get_bootloader_version()));
    add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource,  MCU_GetResetSource()));
    add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource,  esp_reset_reason()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, (uint32_t)MCU_GetWarnings()));
    add_observation_to_collection(observations, create_int32_t_observation(ParamChargeMode, (int32_t)MCU_GetChargeMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));
    //ESP_LOGE(TAG, "\n ************* 1 Sending OperatingMode %d ***************\n", MCU_GetChargeOperatingMode());
    add_observation_to_collection(observations, create_uint32_t_observation(PhaseRotation, (uint32_t)storage_Get_PhaseRotation()));

    add_observation_to_collection(observations, create_uint32_t_observation(HwIdMCUSpeed, (uint32_t)MCU_GetHwIdMCUSpeed()));
    add_observation_to_collection(observations, create_uint32_t_observation(HwIdMCUPower, (uint32_t)MCU_GetHwIdMCUPower()));

    char buf[256];
    GetTimeOnString(buf);
    snprintf(buf + strlen(buf), sizeof(buf), " Boot: ESP: v%s, MCU: v%s  Switch: %d/MaxInst: %2.1fA Sta: %2.1fA  ChargeState: %d  MCnt: %d  BRTC: 0x%X 0x%X Partition: %s", GetSoftwareVersion(), MCU_GetSwVersionString(), MCU_GetSwitchState(), MCU_ChargeCurrentInstallationMaxLimit(), MCU_StandAloneCurrent(), MCU_GetChargeOperatingMode(), MCU_GetDebugCounter(), RTCGetBootValue0(), RTCGetBootValue1(), OTAReadRunningPartition());

    ESP_LOGI(TAG, "Sending charging telemetry: %d/256", strlen(buf));
    add_observation_to_collection(observations, create_observation(808, buf));


    return publish_json(observations);
}

int publish_debug_telemetry_observation_RequestNewStartChargingCommand()
{
    ESP_LOGI(TAG, "sending Request start command telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(SessionIdentifier, chargeSession_GetSessionId()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, CHARGE_OPERATION_STATE_REQUESTING));

    //ESP_LOGE(TAG, "\n ************* 2 Sending OperatingMode %d ***************\n", CHARGE_OPERATION_STATE_REQUESTING);

    return publish_json(observations);
}

int publish_debug_telemetry_observation_ChargingStateParameters()
{
    ESP_LOGD(TAG, "sending ChargeState telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_uint32_t_observation(ParamCableType, (uint32_t)MCU_GetCableType()));
    add_observation_to_collection(observations, create_int32_t_observation(ParamChargeMode, (int32_t)MCU_GetChargeMode()));
    add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)MCU_GetChargeOperatingMode()));

    //ESP_LOGE(TAG, "\n ************* 3 Sending OperatingMode %d ***************\n", MCU_GetChargeOperatingMode());

    return publish_json(observations);
}


int publish_debug_telemetry_observation_WifiParameters()
{
    ESP_LOGD(TAG, "sending LTE telemetry");

    cJSON *observations = create_observation_collection();

    char wifiMAC[18] = {0};
    esp_read_mac((uint8_t*)wifiMAC, 0); //0=Wifi station
    snprintf(wifiMAC, sizeof(wifiMAC), "%02x:%02x:%02x:%02x:%02x:%02x", wifiMAC[0],wifiMAC[1],wifiMAC[2],wifiMAC[3],wifiMAC[4],wifiMAC[5]);

    add_observation_to_collection(observations, create_observation(MacWiFi, wifiMAC));

    return publish_json(observations);
}

int publish_debug_telemetry_observation_LteParameters()
{
    ESP_LOGD(TAG, "sending LTE telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(LteImsi, (char*)LTEGetImsi()));
    //add_observation_to_collection(observations, create_observation(LteMsisdn, "0"));
    add_observation_to_collection(observations, create_observation(LteIccid, (char*)LTEGetIccid()));
    add_observation_to_collection(observations, create_observation(LteImei, (char*)LTEGetImei()));

    return publish_json(observations);
}

/*
 * Use bitmask to pick which observations are to be sedt
 */
int publish_debug_telemetry_observation_TimeAndSchedule(uint8_t bitmask)
{
    ESP_LOGD(TAG, "sending Loc, TZ and Schedule");

    cJSON *observations = create_observation_collection();

    if(bitmask & 0x01)
    	add_observation_to_collection(observations, create_observation(Location, storage_Get_Location()));

    if(bitmask & 0x02)
    	add_observation_to_collection(observations, create_observation(TimeZone, storage_Get_Timezone()));

    if(bitmask & 0x04)
    	add_observation_to_collection(observations, create_observation(TimeSchedule, storage_Get_TimeSchedule()));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_PulseInterval(uint32_t pulseInterval)
{
    cJSON *observations = create_observation_collection();
    add_observation_to_collection(observations, create_uint32_t_observation(PulseInterval, pulseInterval));
    return publish_json(observations);
    //return publish_json_blocked(observations, 10000);
}

static uint32_t txCnt = 0;
static float OPENVoltage = 0.0;
int publish_debug_telemetry_observation_all(double rssi){
    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(ParamInternalTemperature, I2CGetSHT30Temperature()));
    add_observation_to_collection(observations, create_double_observation(ParamHumidity, I2CGetSHT30Humidity()));

    add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, MCU_GetPower()));

    //Only send temperatures periodically when charging is active
    if(MCU_GetChargeMode() == eCAR_CHARGING)
    {


		add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter, MCU_GetEmeterTemperature(0)));
		if(IsUKOPENPowerBoardRevision() == false)
		{
			add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter2, MCU_GetEmeterTemperature(1)));
			add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureEmeter3, MCU_GetEmeterTemperature(2)));
		}
		add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT, MCU_GetTemperaturePowerBoard(0)));
		add_observation_to_collection(observations, create_double_observation(ParamInternalTemperatureT2, MCU_GetTemperaturePowerBoard(1)));
    }

    if(IsUKOPENPowerBoardRevision())
    {
    	OPENVoltage = MCU_GetOPENVoltage();
    	add_observation_to_collection(observations, create_double_observation(ParamOPENVoltage, OPENVoltage));
    }


	add_observation_to_collection(observations, create_double_observation(CommunicationSignalStrength, rssi));
	//add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, (uint32_t)MCU_GetWarnings()));

	/*if(startupMessage == true)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(MCUResetSource, (uint32_t)MCU_GetResetSource()));
		add_observation_to_collection(observations, create_uint32_t_observation(ESPResetSource, (uint32_t)esp_reset_reason()));

		startupMessage = false;
	}*/

	txCnt++;
	char buf[256];
	GetTimeOnString(buf);
	if(IsUKOPENPowerBoardRevision())
	{
		snprintf(buf + strlen(buf), sizeof(buf), " T_EM: %3.2f  T_M: %3.2f %3.2f   OPENV: %3.2f V: %3.2f   I: %2.2f  C%d CM%d MCnt:%d Rs:%d", MCU_GetEmeterTemperature(0), MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1), OPENVoltage, MCU_GetVoltages(0), MCU_GetCurrents(0), MCU_GetChargeMode(), MCU_GetChargeOperatingMode(), MCU_GetDebugCounter(), mqtt_GetNrOfRetransmits());
	}
	else
	{
		snprintf(buf + strlen(buf), sizeof(buf), " T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  C%d CM%d MCnt:%d Rs:%d", MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2), MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1), MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2), MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2), MCU_GetChargeMode(), MCU_GetChargeOperatingMode(), MCU_GetDebugCounter(), mqtt_GetNrOfRetransmits());
	}

	if(storage_Get_DiagnosticsMode() == eNFC_ERROR_COUNT)
	{
		snprintf(buf + strlen(buf), sizeof(buf), " NFC Pass: %d Fail: %d ", GetPassedDetectedCounter(), GetFailedDetectedCounter());
	}

	ESP_LOGI(TAG, "Sending charging telemetry: %d/256", strlen(buf));

	add_observation_to_collection(observations, create_observation(808, buf));

    return publish_json(observations);
}


static bool sendRTC = false;
void SetSendRTC()
{
	sendRTC = true;
}

static bool clearSessionFlag = false;
void SetClearSessionFlag()
{
	clearSessionFlag = true;
}

static uint32_t previousWarnings = 0;
static uint32_t previousNotifications = 0;
static uint8_t previousNetworkType = 0xff;
static float previousChargeCurrentUserMax = 0.0;
static int previousSetPhases = 0;
static uint8_t previousPhaseRotation = 0;
static int8_t previousChargeMode = 0;
static uint8_t previousChargeOperatingMode = 0;
static uint8_t previousIsStandalone = 0xff;
static float previousStandaloneCurrent = -1.0;
static float previousMaxInstallationCurrentConfig = -1.0;
static float previousMaxInstallationCurrentOnFile = -1.0;
static uint8_t previousSwitchState = 0xff;
static uint8_t previousPermanentLock = 0xff;
static uint8_t previousCableType = 0xff;
static float previousPower = -1.0;
static float previousEnergy = -1.0;
static uint32_t previousDiagnosticsMode = 0;

static uint8_t previousOfflinePhase = 0;
static float previousOfflineCurrent = -1.0;

static float warningValue = 0;
static uint8_t maxRTCSend = 0;
static uint8_t previousFinalStopActiveStatus = 0xff;

static uint32_t previousTransmitInterval = 0;
//static uint32_t previousPulseInterval = 0;
static uint32_t previousCertificateVersion = 0;
static uint8_t previousMaxCurrentConfigSource = 0xff;
static uint32_t previousNumberOfTagsCount = 0;

static uint8_t previousOverrideGridType = 0xff;
static uint8_t previousIT3OptimizationEnabled = 0xff;
static bool previousPingReplyState = 1;
static uint32_t previousMaxStartDelay = 0;
static int8_t sendUpdateInSeconds = 0;
static bool sendPower = false;
static float powerLimit = 0.0;

int publish_telemetry_observation_on_change(){
    ESP_LOGD(TAG, "sending on change telemetry");

    bool isChange = false;

    cJSON *observations = create_observation_collection();


    uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();
	if (((previousChargeOperatingMode != chargeOperatingMode) && (chargeOperatingMode != 0)))// || (clearSessionFlag == true))
	{

		//If a disconnect occur, send 0 voltage levels
		if (chargeOperatingMode == CHARGE_OPERATION_STATE_DISCONNECTED)
		{
			add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase1, MCU_GetVoltages(0)));
			if(IsUKOPENPowerBoardRevision() == false)
			{
				add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase2, MCU_GetVoltages(1)));
				add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase3, MCU_GetVoltages(2)));
			}
		}

		//If we have received command SetSessionId = null from cloud, fake car disconnect to clear session and user id in cloud
		/*if(clearSessionFlag == true)
		{
			chargeOperatingMode = CHARGE_OPERATION_STATE_DISCONNECTED;
			clearSessionFlag = false;
		}*/

		add_observation_to_collection(observations, create_uint32_t_observation(ParamChargeOperationMode, (uint32_t)chargeOperatingMode));

		//ESP_LOGE(TAG, "\n ************* 4 Sending OperatingMode %d ***************\n", chargeOperatingMode);
		previousChargeOperatingMode = chargeOperatingMode;
		isChange = true;
	}

    int8_t chargeMode = MCU_GetChargeMode();
	if ((previousChargeMode != chargeMode) && (chargeMode != 0) && (chargeMode != -1))
	{
		add_observation_to_collection(observations, create_int32_t_observation(ParamChargeMode, (int32_t)chargeMode));
		previousChargeMode = chargeMode;
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
    if (previousNetworkType != networkType)
    {
    	add_observation_to_collection(observations, create_uint32_t_observation(ParamNetworkType, (uint32_t)networkType));

    	//Always send this also to ensure consistency
    	add_observation_to_collection(observations, create_uint32_t_observation(MaxPhases, (uint32_t)GetMaxPhases()));
    	previousNetworkType = networkType;
    	isChange = true;
    }

    uint32_t warnings = MCU_GetWarnings();
    if(previousWarnings != warnings)
    {
		if(IsUKOPENPowerBoardRevision())
		{
			if(((warnings & 0x400000) && !(previousWarnings & 0x400000)) || (!(warnings & 0x400000) && (previousWarnings & 0x400000)))
			{
				ESP_LOGI(TAG, "Sending O-PEN voltage on warning change: 0x%06X, 0x%06X", warnings, previousWarnings);
				add_observation_to_collection(observations, create_double_observation(ParamOPENVoltage, MCU_GetOPENVoltage()));
			}
		}

    	add_observation_to_collection(observations, create_uint32_t_observation(ParamWarnings, warnings));
    	previousWarnings = warnings;
    	isChange = true;
    }

    if(warnings != 0)
    {
    	if(warningValue == 0.0)
    	{
    		ZapMessage rxMsg = MCU_ReadParameter(ParamWarningValue);
			if((rxMsg.identifier == ParamWarningValue) && (rxMsg.length == 4))
			{
				warningValue = GetFloat(rxMsg.data);
				add_observation_to_collection(observations, create_uint32_t_observation(ParamWarningValue, warningValue));
			}
    	}
    }
    else
    {
    	warningValue = 0.0;
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

	float maxInstallationCurrentConfig = MCU_ChargeCurrentInstallationMaxLimit();
	float maxInstallationCurrentOnFile = storage_Get_MaxInstallationCurrentConfig();
	if((previousMaxInstallationCurrentConfig != maxInstallationCurrentConfig) || (previousMaxInstallationCurrentOnFile != maxInstallationCurrentOnFile))
	{
		float valueToSend = 0.0;
		if((maxInstallationCurrentOnFile <= 40.0) && (maxInstallationCurrentOnFile > 32.0))
		{
			valueToSend = maxInstallationCurrentOnFile;
			ESP_LOGW(TAG, "Sending ESP value: %f", maxInstallationCurrentOnFile);
		}
		else
		{
			valueToSend = maxInstallationCurrentConfig;
			ESP_LOGW(TAG, "Sending MCUs MaxInstCurrent: %f", maxInstallationCurrentConfig);
		}

		add_observation_to_collection(observations, create_double_observation(ChargeCurrentInstallationMaxLimit, valueToSend));
		previousMaxInstallationCurrentConfig = maxInstallationCurrentConfig;
		previousMaxInstallationCurrentOnFile = maxInstallationCurrentOnFile;
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

	/// In Watts
	if (power > 7000.0)
		powerLimit = 500.0;
	else
		powerLimit = 200.0;

	/// Evaluate conditions for sending power, to avoid frequent transmission, but give good accuracy

	if(((power > previousPower + powerLimit) || (power < (previousPower - powerLimit))) && (sendUpdateInSeconds == 0))
	{
		sendUpdateInSeconds = 5;
	}

	if((power == 0.0) && (previousPower > 0.0))
	{
		sendPower = true;
	}

	/// This delays sending the power value to let it stabilize. Avoids sending often during frequent change to save data/server load
	if(sendUpdateInSeconds > 0)
	{
		sendUpdateInSeconds--;
		ESP_LOGI(TAG, "Delay power: %i", sendUpdateInSeconds);
		if(sendUpdateInSeconds == 0)
		{
			sendPower = true;
		}
	}

	if(sendPower == true)
	{
		sendPower = false;
		sendUpdateInSeconds = 0;

		add_observation_to_collection(observations, create_double_observation(ParamTotalChargePower, power));

		float currents[3] = {0};
		currents[0] = MCU_GetCurrents(0);
		currents[1] = MCU_GetCurrents(1);
		currents[2] = MCU_GetCurrents(2);

		//On Pro the IT3 wiring is described as PE, N=L3, L1, L2
		//On Go  the IT3 wiring is described as PE, N=L1, L2, L3
		//To make the currents reported on correct phase seen from main fuse/APM/AMS they must be
		// remapped for IT3-phase on Go.
		//This means load on Type 2s L1 and L2 will show the combined emeter current on N which is wired to L1 on main fuse
		if(MCU_GetGridType() == NETWORK_3P3W)
		{
			struct ThreePhaseResult result = CalculatePhasePairCurrentFromPhaseCurrent(currents[0], currents[1], currents[2]);
			/* Original "Pro" mapping
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, result.L3_L1));
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, result.L3_L2));
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, result.L1_L2));
			*/
			//Mapping because of Go wiring
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, result.L1_L2)); //The value measured on N(L3) must be shown on L1;
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, result.L3_L1)); //The value measured on L1 must be shown on L2;
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, result.L3_L2)); //The value measured on L2 must be shown on L3;

			ESP_LOGW(TAG, "IT3-Phase: Meas: %2.2f %2.2f %2.2f  Calc: %2.2f %2.2f %2.2f  %d", currents[0],currents[1],currents[2], result.L3_L1, result.L3_L2, result.L1_L2, result.usedAlgorithm);

			/*char buf[256];
			sprintf(buf, "IT3-Phase: %2.fW Meas: %2.2f %2.2f %2.2f  Calc: %2.2f %2.2f %2.2f  %d", power, currents[0],currents[1],currents[2], result.L3_L1, result.L3_L2, result.L1_L2, result.usedAlgorithm);
			//ESP_LOGI(TAG, "Sending charging telemetry: %d/256", strlen(buf));
			add_observation_to_collection(observations, create_observation(808, buf));*/
		}
		else
		{
			//When power changes also update currents to be responsive
			add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase1, currents[0]));
			if(IsUKOPENPowerBoardRevision() == false)
			{
				add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase2, currents[1]));
				add_observation_to_collection(observations, create_double_observation(ParamCurrentPhase3, currents[2]));
			}
		}

		ESP_LOGI(TAG, "Sending power: %i: %4.2f W (%4.2f)", sendUpdateInSeconds, power, previousPower);

		previousPower = power;
		isChange = true;
	}

	float energy = chargeSession_Get().Energy;

	/// Round down on the 3rd decmial to ensure. In OCMF this digit may be rounded down to meet the SignedValue diff
	/// to compensate for rounding inaccuracy. By always rounding down here it ensures that this value can never be
	/// 1Wh higher than the CompletedSession energy.
	energy = floor(energy * 1000) / 1000.0;

	//Send energy for every 0.1kWh when below 1kWh to make the app responsive at start of charging
	//Send for every 0.2kWh above 1kWh
	float trigLimit = 0.1;
	if(energy >= 1.0)
		trigLimit = 0.2;
	if(energy >= 5.0)
		trigLimit = 0.5;

	//Send energy based on level of change
	if((energy > previousEnergy + trigLimit) || (energy < (previousEnergy - trigLimit)))
	{
		/// Sanity check
		if(energy < 0.0)
			energy = 0.0;

		add_observation_to_collection(observations, create_double_observation(ParamTotalChargePowerSession, energy));

		/// When 0.1kWh has been consumed, send voltages once
		if ((energy >= 0.1) && (previousEnergy < 0.1))
		{
			add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase1, MCU_GetVoltages(0)));
			if(IsUKOPENPowerBoardRevision() == false)
			{
				add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase2, MCU_GetVoltages(1)));
				add_observation_to_collection(observations, create_double_observation(ParamVoltagePhase3, MCU_GetVoltages(2)));
			}
		}

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

	if((RTCIsRegisterChanged() || sendRTC) && (maxRTCSend < 10)) //If there is an I2C bus error, don't send unlimited nr of messages.
	{
		char buf[80];
		snprintf(buf, sizeof(buf)," RTC: %i 0x%X->0x%X %i 0x%X->0x%X", RTCGetValueCheckCounter0(), RTCGetLastIncorrectValue0(), RTCGetLastValue0(), RTCGetValueCheckCounter1(), RTCGetLastIncorrectValue1(), RTCGetLastValue1());

		ESP_LOGI(TAG, "Sending RTC telemetry: %d/80", strlen(buf));

		add_observation_to_collection(observations, create_observation(808, buf));

		sendRTC = false;

		isChange = true;

		maxRTCSend++;
	}

	uint8_t offlinePhase = storage_Get_DefaultOfflinePhase();
	if(previousOfflinePhase != offlinePhase)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ChargerOfflinePhase, (uint32_t)offlinePhase));
		previousOfflinePhase = offlinePhase;
		isChange = true;
	}

	float offlineCurrent = storage_Get_DefaultOfflineCurrent();
	if((previousOfflineCurrent != offlineCurrent))
	{
		add_observation_to_collection(observations, create_double_observation(ChargerOfflineCurrent, offlineCurrent));
		previousOfflineCurrent = offlineCurrent;
		isChange = true;
	}

	uint8_t finalStopActiveStatus = GetFinalStopActiveStatus();
	if(finalStopActiveStatus != previousFinalStopActiveStatus)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(FinalStopActive, (uint32_t)finalStopActiveStatus));
		previousFinalStopActiveStatus = finalStopActiveStatus;
		isChange = true;
	}

    uint32_t transmitInterval = storage_Get_TransmitInterval();
    if(previousTransmitInterval != transmitInterval)
    {
    	add_observation_to_collection(observations, create_uint32_t_observation(TransmitInterval, transmitInterval));
    	previousTransmitInterval = transmitInterval;
    	isChange = true;
    }

	/*uint32_t pulseInterval = storage_Get_PulseInterval();
	if(previousPulseInterval != pulseInterval)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(PulseInterval, pulseInterval));
		previousPulseInterval = pulseInterval;
		isChange = true;
	}*/

	uint32_t certificateVersion = (uint32_t)certificate_GetCurrentBundleVersion();
	if(previousCertificateVersion != certificateVersion)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(CertificateVersion, certificateVersion));
		previousCertificateVersion = certificateVersion;
		isChange = true;
	}

	uint8_t maxCurrentConfigSource = GetMaxCurrentConfigurationSource();
	if(previousMaxCurrentConfigSource != maxCurrentConfigSource)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(MaxCurrentConfigurationSource, (uint32_t)maxCurrentConfigSource));
		previousMaxCurrentConfigSource = maxCurrentConfigSource;
		isChange = true;
	}

	uint32_t nrOfTagsCount = storage_GetNrOfTagsCounter();
	if(previousNumberOfTagsCount != nrOfTagsCount)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(NrOfChargeCards, nrOfTagsCount));
		previousNumberOfTagsCount = nrOfTagsCount;
		isChange = true;
	}

	uint8_t overrideGridType = MCU_GetOverrideGridType();
	if(overrideGridType != previousOverrideGridType)
	{
		add_observation_to_collection(observations, create_uint32_t_observation(ParamGridTypeOverride, (uint32_t)overrideGridType));
		previousOverrideGridType = overrideGridType;
		isChange = true;
	}

	//Only send the IT3 Optimization state when on IT3 grid
	if(MCU_GetGridType() == NETWORK_3P3W)
	{
		uint8_t IT3OptimizationEnabled = MCU_GetIT3OptimizationState();
		if(IT3OptimizationEnabled != previousIT3OptimizationEnabled)
		{
			add_observation_to_collection(observations, create_uint32_t_observation(ParamIT3OptimizationEnabled, (uint32_t)IT3OptimizationEnabled));
			previousIT3OptimizationEnabled = IT3OptimizationEnabled;
			isChange = true;
		}
	}

	if(storage_Get_Standalone() == false)
	{
		bool pingReplyState = offlineHandler_IsPingReplyOffline();
		if(pingReplyState != previousPingReplyState)
		{
			add_observation_to_collection(observations, create_uint32_t_observation(OfflineMode, (uint32_t)pingReplyState));
			previousPingReplyState = pingReplyState;
			isChange = true;
		}
	}

	if(chargeController_CheckForNewScheduleEvent())
	{
		add_observation_to_collection(observations, create_observation(NextScheduleEvent, chargeController_GetNextStartString()));
		isChange = true;
	}

	if(BLE_CheckForNewLocation())
	{
		add_observation_to_collection(observations, create_observation(Location, storage_Get_Location()));
		isChange = true;
	}


	if(BLE_CheckForNewTimezone())
	{
		add_observation_to_collection(observations, create_observation(TimeZone, storage_Get_Timezone()));
		isChange = true;
	}

	if(BLE_CheckForNewTimeSchedule())
	{
		add_observation_to_collection(observations, create_observation(TimeSchedule, storage_Get_TimeSchedule()));
		isChange = true;
	}

	if(strncmp(storage_Get_Location(), "GBR", 3) == 0)
	{
		uint32_t maxStartDelay = storage_Get_MaxStartDelay();
		if(previousMaxStartDelay != maxStartDelay)
		{
			add_observation_to_collection(observations, create_uint32_t_observation(MaxStartDelay, maxStartDelay));
			previousMaxStartDelay = maxStartDelay;
			isChange = true;
		}
	}


	//Check ret and retry?
    int ret = 0;

    if(isChange == true)
    	ret = publish_json(observations);
    else
    	cJSON_Delete(observations);

    return ret;
}


void SendStacks()
{
	char buf[150] = {0};
	snprintf(buf, sizeof(buf),"Stacks: i2c:%d mcu:%d %d adc: %d, lte: %d conn: %d, sess: %d, ocmf: %d", I2CGetStackWatermark(), MCURxGetStackWatermark(), MCUTxGetStackWatermark(), adcGetStackWatermark(), pppGetStackWatermark(), connectivity_GetStackWatermark(), sessionHandler_GetStackWatermark(), sessionHandler_GetStackWatermarkOCMF());
	publish_debug_telemetry_observation_Diagnostics(buf);
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

int publish_string_observation_blocked(int observationId, char *message, int timeout_ms){
    return publish_json_blocked(create_observation(observationId, message), timeout_ms);
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
