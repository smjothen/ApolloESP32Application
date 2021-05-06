#ifndef ZAPTEC_CLOUD_LISTENER_H
#define ZAPTEC_CLOUD_LISTENER_H

#include "../../main/DeviceInfo.h"

void MqttSetDisconnected();
bool isMqttConnected();
void cloud_listener_check_cmd();
void start_cloud_listener_task(struct DeviceInfo deviceInfo);
void stop_cloud_listener_task();
bool CloudSettingsAreUpdated();
void ClearCloudSettingsAreUpdated();
bool LocalSettingsAreUpdated();
void ClearLocalSettingsAreUpdated();

void ClearCloudCommandCurrentUpdated();
bool CloudCommandCurrentUpdated();

bool GetReportGridTestResults();
void ClearReportGridTestResults();

bool GetMCUDiagnosticsResults();
void ClearMCUDiagnosicsResults();

bool GetESPDiagnosticsResults();
void ClearESPDiagnosicsResults();

bool GetInstallationConfigOnFile();
void ClearInstallationConfigOnFile();

void ClearNewInstallationIdFlag();
bool GetNewInstallationIdFlag();

int publish_iothub_event(const char *payload);
int publish_iothub_event_blocked(const char* payload, TickType_t xTicksToWait);
int publish_to_iothub(const char* payload, const char* topic);

void update_installationId();


#endif /* ZAPTEC_CLOUD_LISTENER_H */
