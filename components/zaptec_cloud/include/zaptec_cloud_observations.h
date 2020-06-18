#ifndef ZAPTEC_CLOUD_OBSERVATIONS_H
#define ZAPTEC_CLOUD_OBSERVATIONS_H

int publish_debug_telemetry_observation(
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3,
    double temperature_5, double temperature_emeter
);

int publish_debug_message_event(char *message);

#endif /* ZAPTEC_CLOUD_OBSERVATIONS_H */
