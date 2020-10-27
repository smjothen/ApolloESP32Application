#ifndef ZAPTEC_CLOUD_OBSERVATIONS_H
#define ZAPTEC_CLOUD_OBSERVATIONS_H

int publish_debug_telemetry_observation(
    double temperature_5, double temperature_emeter, double rssi
);

int publish_debug_telemetry_observation_power(
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3
);

int publish_debug_telemetry_observation_cloud_settings();

int publish_debug_telemetry_observation_local_settings();

int publish_debug_telemetry_observation_NFC_tag_id(char * NFCHexString);

int publish_debug_telemetry_observation_CompletedSession(char * CompletedSessionString);

int publish_debug_telemetry_observation_StartUpParameters();

int publish_debug_telemetry_observation_all(
	double temperature_emeter1, double temperature_emeter2, double temperature_emeter3,
	double temperature_TM, double temperature_TM2,
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3,
	double rssi
);

typedef enum {
    cloud_event_level_error = 10,
    cloud_event_level_information = 30,
    cloud_event_level_warning = 20
} cloud_event_level;


int publish_uint32_observation(int observationId, uint32_t value);
int publish_double_observation(int observationId, double value);
int publish_diagnostics_observation(char *message);
int publish_debug_message_event(char *message, cloud_event_level level);
int publish_cloud_pulse(void);

int publish_noise(void);

#endif /* ZAPTEC_CLOUD_OBSERVATIONS_H */
