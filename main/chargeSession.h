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
	char AuthenticationCode[37];//Up to GUID string.
	uint32_t unixStartTime;
};


char* chargeSession_GetSessionId();
bool chargeSession_HasNewSessionId();
void chargeSession_ClearHasNewSession();

void chargeSession_Start();
void chargeSession_UpdateEnergy();
void chargeSession_Finalize();
void chargeSession_Clear();
void chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud);
void chargeSession_SetAuthenticationCode(char * idAsString);
void chargeSession_SetEnergy(float energy);

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message);

esp_err_t chargeSession_SaveSessionResetInfo();
esp_err_t chargeSession_ReadSessionResetInfo();

#ifdef __cplusplus
}
#endif

#endif  /*_CHARGESESSION_H_*/
