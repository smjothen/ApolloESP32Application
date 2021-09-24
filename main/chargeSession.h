#ifndef _CHARGESESSION_H_
#define _CHARGESESSION_H_

#ifdef __cplusplus
extern "C" {
#endif


struct ChargeSession
{
	char SessionId[37]; //[8-4-4-4-12 + 1]
	float Energy;
	char StartTime[32]; //27
	char EndTime[32]; //27
	bool ReliableClock;
	bool StoppedByRFID;
	char AuthenticationCode[41];//Up to GUID string.
	uint32_t unixStartTime;
	char * SignedSession;//[1000];//8
};

void chargeSession_PrintSession();
char* chargeSession_GetSessionId();
bool chargeSession_HasNewSessionId();
void chargeSession_ClearHasNewSession();

void chargeSession_Start();
void chargeSession_UpdateEnergy();
void chargeSession_Finalize();
void chargeSession_Clear();
void chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud);
void chargeSession_SetAuthenticationCode(char * idAsString);
void chargeSession_ClearAuthenticationCode();
void chargeSession_SetEnergy(float energy);
void chargeSession_SetStoppedByRFID(bool stoppedByRFID);
void chargeSession_SetOCMF(char * OCMDString);

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message);

esp_err_t chargeSession_SaveSessionResetInfo();
esp_err_t chargeSession_ReadSessionResetInfo();

bool chargeSession_IsAuthenticated();

#ifdef __cplusplus
}
#endif

#endif  /*_CHARGESESSION_H_*/
