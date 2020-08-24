#ifndef ZAPTEC_CLOUD_LISTENER_H
#define ZAPTEC_CLOUD_LISTENER_H

#include "../../main/DeviceInfo.h"

void start_cloud_listener_task(struct DeviceInfo deviceInfo);

int publish_iothub_event(const char *payload);
int publish_to_iothub(const char* payload, const char* topic);

int publish_iothub_ack(const char *payload);

#endif /* ZAPTEC_CLOUD_LISTENER_H */
