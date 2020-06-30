#ifndef ZAPTEC_CLOUD_LISTENER_H
#define ZAPTEC_CLOUD_LISTENER_H

void start_cloud_listener_task(void);

int publish_iothub_event(char *payload);

#endif /* ZAPTEC_CLOUD_LISTENER_H */
