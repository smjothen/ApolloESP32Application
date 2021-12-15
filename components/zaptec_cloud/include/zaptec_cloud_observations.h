#ifndef ZAPTEC_CLOUD_OBSERVATIONS_H
#define ZAPTEC_CLOUD_OBSERVATIONS_H

struct MqttDataDiagnostics
{
	uint32_t mqttBytes;
	uint32_t mqttBytesIncMeta;
	uint32_t nrOfmessages;
};

struct MqttDataDiagnostics MqttGetDiagnostics();
void MqttDataReset();

int publish_debug_telemetry_observation(
    double temperature_5, double temperature_emeter, double rssi
);

int publish_debug_telemetry_observation_power();

int publish_debug_telemetry_observation_cloud_settings();

int publish_debug_telemetry_observation_local_settings();

int publish_debug_telemetry_observation_NFC_tag_id(char * NFCHexString);

int publish_debug_telemetry_observation_AddNewChargeCard(char * NewChargeCardString);

int publish_debug_telemetry_observation_CompletedSession(char * CompletedSessionString);

int publish_debug_telemetry_observation_GridTestResults(char * gridTestResults);

int publish_debug_telemetry_observation_Diagnostics(char * diagnostics);

int publish_debug_telemetry_observation_InstallationConfigOnFile();

int publish_debug_telemetry_observation_StartUpParameters();

int publish_debug_telemetry_observation_RequestNewStartChargingCommand();

int publish_debug_telemetry_observation_ChargingStateParameters();

int publish_debug_telemetry_observation_WifiParameters();

int publish_debug_telemetry_observation_LteParameters();

int publish_debug_telemetry_observation_all(double rssi);

void SetClearSessionFlag();

int publish_telemetry_observation_on_change();

void SendStacks();

void SetSendRTC();

typedef enum {
    cloud_event_level_error = 10,
    cloud_event_level_information = 30,
    cloud_event_level_warning = 20
} cloud_event_level;


int publish_uint32_observation(int observationId, uint32_t value);
int publish_double_observation(int observationId, double value);
int publish_string_observation(int observationId, char *message);
int publish_string_observation_blocked(int observationId, char *message, int timeout_ms);
int publish_diagnostics_observation(char *message);
int publish_debug_message_event(char *message, cloud_event_level level);
int publish_cloud_pulse(void);

int publish_noise(void);

int publish_prodtest_line(char *message);

#endif /* ZAPTEC_CLOUD_OBSERVATIONS_H */
