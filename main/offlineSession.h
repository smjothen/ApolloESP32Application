#ifndef OFFLINE_SESSION
#define OFFLINE_SESSION
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include "esp_system.h"
#include "cJSON.h"
#include "chargeSession.h"

void offlineSession_Init();
void offlineSession_AppendLogString(char * stringToAdd);
void offlineSession_AppendLogStringWithInt(char * stringToAdd, int value);
void offlineSession_AppendLogStringWithIntInt(char * stringToAdd, int value1, int value2);
void offlineSession_AppendLogStringErr();
void offlineSession_AppendLogLength();
char * offlineSession_GetLog();
void offlineSession_ClearLog();

bool offlineSession_CheckFilesSystem();
bool offlineSession_test_CreateFile();
bool offlineSession_test_DeleteFile();
void offlineSession_ClearDiagnostics();
char * offlineSession_GetDiagnostics();
bool offlineSession_FileSystemVerified();
bool offlineSession_CheckAndCorrectFilesSystem();
bool offlineSession_FileSystemCorrected();
bool offlineSession_eraseAndRemountPartition();
bool offlineSession_mount_folder();
void offlineSession_disable(void);

int offlineSession_FindNewFileNumber();
int offlineSession_FindOldestFile();
int offlineSession_FindNrOfFiles();
int offlineSession_GetMaxSessionCount();

int offlineSession_CheckIfLastLessionIncomplete(struct ChargeSession *incompleteSession);

void offlineSession_SetSessionFileInactive();
void offlineSession_DeleteLastUsedFile();

esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo);

cJSON * offlineSession_ReadChargeSessionFromFile(int fileNo, bool *isMid, uint32_t *sessionId);
cJSON* offlineSession_GetSignedSessionFromActiveFile(int fileNo);

int offlineSession_UpdateSessionOnFile(char *sessionData, bool createNewFile);
int offlineSession_UpdateSessionOnFileMID(char *sessionData, bool createNewFile, uint32_t sessionId);

esp_err_t offlineSession_SaveSession(char * sessionData);
esp_err_t offlineSession_SaveSessionMID(char * sessionData, uint32_t sessionId);

void offlineSession_append_energy(char label, time_t timestamp, double energy);

void offlineSession_DeleteAllFiles();
int offlineSession_delete_session(int fileNo);
double GetEnergySigned();

#endif /* OFFLINE_SESSION */
