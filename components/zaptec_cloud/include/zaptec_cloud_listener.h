#ifndef ZAPTEC_CLOUD_LISTENER_H
#define ZAPTEC_CLOUD_LISTENER_H

void start_cloud_listener_task(void);

int publish_iothub_event(const char *payload);
int publish_to_iothub(const char* payload, const char* topic);

#endif /* ZAPTEC_CLOUD_LISTENER_H */
