#ifndef ZAPTEC_CLOUD_OBSERVATIONS_H
#define ZAPTEC_CLOUD_OBSERVATIONS_H

int publish_debug_telemetry_observation(
    double temperature_5, double temperature_emeter, double rssi
);

int publish_debug_telemetry_observation_power(
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3
);

typedef enum {
    cloud_event_level_error = 10,
    cloud_event_level_information = 30,
    cloud_event_level_warning = 20
} cloud_event_level;

int publish_debug_message_event(char *message, cloud_event_level level);
int publish_cloud_pulse(void);

#endif /* ZAPTEC_CLOUD_OBSERVATIONS_H */
