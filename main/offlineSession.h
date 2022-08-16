#ifndef OFFLINE_SESSION
#define OFFLINE_SESSION
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include "esp_system.h"
#include "cJSON.h"
#include "chargeSession.h"

void offlineSession_Init();
bool offlineSession_mount_folder();

int offlineSession_FindNewFileNumber();
int offlineSession_FindOldestFile();
long offlineSession_FindOldestFile_ocpp();
time_t offlineSession_PeekNextMessageTimestamp_ocpp();
int offlineSession_FindNrOfFiles();
int offlineSession_FindNrOfFiles_ocpp();
int offlineSession_GetMaxSessionCount();

int offlineSession_CheckIfLastLessionIncomplete(struct ChargeSession *incompleteSession);

void offlineSession_SetSessionFileInactive();
void offlineSession_DeleteLastUsedFile();
void offlineSession_UpdateSessionOnFile(char *sessionData, bool createNewFile);
esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo);

cJSON * offlineSession_ReadChargeSessionFromFile(int fileNo);
cJSON * offlineSession_ReadNextMessage_ocpp(void ** cb_data);
cJSON* offlineSession_GetSignedSessionFromActiveFile(int fileNo);

esp_err_t offlineSession_SaveSession(char * sessionData);
esp_err_t offlineSession_SaveStartTransaction_ocpp(int transaction_id, time_t transaction_start_timestamp, int connector_id,
						const char * id_tag, int meter_start, int * reservation_id);
esp_err_t offlineSession_SaveStopTransaction_ocpp(int transaction_id, time_t transaction_start_timestamp, const char * id_tag,
						int meter_stop, time_t timestamp, const char * reason);
esp_err_t offlineSession_SaveNewMeterValue_ocpp(int transaction_id, time_t transaction_start_timestamp, const unsigned char * meter_buffer, size_t buffer_length);
esp_err_t offlineSession_UpdateTransactionId_ocpp(int old_transaction_id, int new_transaction_id);

void offlineSession_append_energy(char label, int timestamp, double energy);
int offlineSession_attempt_send(void);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);
void offlineSession_DeleteAllFiles();
int offlineSession_DeleteAllFiles_ocpp();
int offlineSession_delete_session(int fileNo);
double GetEnergySigned();

#endif /* OFFLINE_SESSION */
