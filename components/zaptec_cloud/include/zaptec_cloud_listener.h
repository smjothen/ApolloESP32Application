#ifndef ZAPTEC_CLOUD_LISTENER_H
#define ZAPTEC_CLOUD_LISTENER_H

#include "../../main/DeviceInfo.h"

void MqttSetDisconnected();
void MqttSetSimulatedOffline(bool simOffline);
bool isMqttConnected();
void cloud_listener_check_cmd();
void start_cloud_listener_task(struct DeviceInfo deviceInfo);
void stop_cloud_listener_task();
bool CloudSettingsAreUpdated();
void ClearCloudSettingsAreUpdated();
bool LocalSettingsAreUpdated();
void ClearLocalSettingsAreUpdated();
int RFIDListIsUpdated();
void ClearRfidListIsUpdated();

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

bool GetDatalog();
bool cloud_listener_test_reconnect();
int InitiateOTASequence();
bool IsOTADelayActive();
void ClearOTADelay();

int publish_iothub_event(const char *payload);
int publish_iothub_event_blocked(const char* payload, TickType_t xTicksToWait);
int publish_to_iothub(const char* payload, const char* topic);

void update_installationId();
void update_mqtt_event_pattern(bool usePingReply);

void periodic_refresh_token(uint8_t source);

int cloud_listener_GetResetCounter();
void cloud_listener_IncrementResetCounter();

void cloud_listener_SetMQTTKeepAliveTime(uint8_t isStandalone);


#endif /* ZAPTEC_CLOUD_LISTENER_H */
