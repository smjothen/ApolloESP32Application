#ifndef OFFLINE_SESSION
#define OFFLINE_SESSION
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include "esp_system.h"
#include "cJSON.h"

bool offlineSession_mount_folder();

int offlineSession_FindLatestFile();
int offlineSession_FindOldestFile();
int offlineSession_FindNrOfFiles();

void offlineSession_UpdateSessionOnFile(char *sessionData);
esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo);
esp_err_t offlineSession_ReadChargeSessionFromFile(int fileNo, cJSON * jsonSession);
esp_err_t offlineSession_SaveSession(char * sessionData);


void offlineSession_append_energy(char label, int timestamp, double energy);
int offlineSession_attempt_send(void);

esp_err_t offlineSession_GetSignedSessionFromActiveFile(cJSON* entryArray);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);
int offlineSession_delete_session(int fileNo);

#endif /* OFFLINE_SESSION */
