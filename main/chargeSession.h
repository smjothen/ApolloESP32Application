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

void chargeSession_PrintSession(bool online, bool pingReplyActive);
char* chargeSession_GetSessionId();
bool chargeSession_HasNewSessionId();
void chargeSession_ClearHasNewSession();
void chargeSession_CheckIfLastSessionIncomplete();
void chargeSession_Start();
void chargeSession_UpdateEnergy();
void chargeSession_Finalize();
void chargeSession_Clear();
int8_t chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud);
void chargeSession_SetAuthenticationCode(char * idAsString);
void chargeSession_ClearAuthenticationCode();
//void chargeSession_SetEnergy(float energy);
void chargeSession_SetStoppedByRFID(bool stoppedByRFID);
void chargeSession_SetOCMF(char * OCMDString);

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message);

esp_err_t chargeSession_SaveUpdatedSession();
//esp_err_t chargeSession_ReadSessionResetInfo();

bool chargeSession_IsAuthenticated();
void SetCarConnectedState(bool connectedState);
bool chargeSession_IsCarConnected();
void chargeSession_SetReceivedStartChargingCommand();

#ifdef __cplusplus
}
#endif

#endif  /*_CHARGESESSION_H_*/
