#ifndef _CHARGESESSION_H_
#define _CHARGESESSION_H_

#ifdef __cplusplus
extern "C" {
#endif


struct ChargeSession
{
	char SessionId[37]; //[8-4-4-4-12 + 1]
	float Energy;
	char StartDateTime[32]; //27
	char EndDateTime[32]; //27
	bool ReliableClock;
	bool StoppedByRFID;
	char AuthenticationCode[41];//Up to GUID string.
	uint32_t unixStartTime;
	time_t EpochStartTimeSec;
	uint32_t EpochStartTimeUsec;
	time_t EpochEndTimeSec;
	uint32_t EpochEndTimeUsec;

	char * SignedSession;
};

struct HoldSessionStartTime {
	char timeString[32];
	time_t holdEpochSec;
	uint32_t holdEpochUsec;
	bool usedInSession;
	bool usedInRequest;
};

void chargeSession_PrintSession(bool online, bool pingReplyActive);
char* chargeSession_GetSessionId();
bool chargeSession_HasNewSessionId();
void chargeSession_ClearHasNewSession();
void chargeSession_CheckIfLastSessionIncomplete();
void chargeSession_Start();
void chargeSession_UpdateEnergy();
void chargeSession_Finalize();
void chargeSession_Clear();
bool chargeSession_IsLocalSession();
int8_t chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud);
void chargeSession_SetAuthenticationCode(char * idAsString);
char* chargeSession_GetAuthenticationCode();
void chargeSession_ClearAuthenticationCode();
//void chargeSession_SetEnergy(float energy);
void chargeSession_SetStoppedByRFID(bool stoppedByRFID);
void chargeSession_SetEnergyForTesting(float e);
void chargeSession_SetOCMF(char * OCMDString);
void SetUUIDFlagAsCleared();
void chargeSession_HoldUserUUID();
char * sessionSession_GetHeldUserUUID();
bool sessionSession_IsHoldingUserUUID();
void chargeSession_ClearHeldUserUUID();

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message);

esp_err_t chargeSession_SaveUpdatedSession();
//esp_err_t chargeSession_ReadSessionResetInfo();

bool chargeSession_IsAuthenticated();
bool chargeSession_HasSessionId();

void SetCarConnectedState(bool connectedState);
bool chargeSession_IsCarConnected();

#ifdef __cplusplus
}
#endif

#endif  /*_CHARGESESSION_H_*/
