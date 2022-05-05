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
int offlineSession_FindNrOfFiles();

int offlineSession_CheckIfLastLessionIncomplete(struct ChargeSession *incompleteSession);

void offlineSession_SetSessionFileInactive();
void offlineSession_UpdateSessionOnFile(char *sessionData, bool createNewFile);
esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo);

cJSON * offlineSession_ReadChargeSessionFromFile(int fileNo);
cJSON* offlineSession_GetSignedSessionFromActiveFile(int fileNo);

esp_err_t offlineSession_SaveSession(char * sessionData);

void offlineSession_append_energy(char label, int timestamp, double energy);
int offlineSession_attempt_send(void);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);
int offlineSession_delete_session(int fileNo);
double GetEnergyDiff();

#endif /* OFFLINE_SESSION */
