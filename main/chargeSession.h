#ifndef _CHARGESESSION_H_
#define _CHARGESESSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../components/ocpp/include/types/ocpp_reason.h"
#include "../components/uuid/include/uuid.h"

struct ChargeSession
{
	char SessionId[37]; //[8-4-4-4-12 + 1]
	float Energy;
	char StartDateTime[32]; //27
	char EndDateTime[32]; //27
	bool ReliableClock;
	bool StoppedByRFID;
	char StoppedById[21];
	enum ocpp_reason_id StoppedReason;
	char AuthenticationCode[41];//Up to GUID string.
	char parent_id[21];
	uint32_t unixStartTime;
	time_t EpochStartTimeSec;
	uint32_t EpochStartTimeUsec;
	time_t EpochEndTimeSec;
	uint32_t EpochEndTimeUsec;

	bool HasMIDSessionId;
	uint32_t MIDSessionId;

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
bool chargeSession_GetFileError();
void chargeSession_SetTestFileCorrection();
uuid_t chargeSession_Start(bool isMid, uint32_t sessionId);
float chargeSession_GetEnergy();
void chargeSession_UpdateEnergy();
void chargeSession_Finalize();
void chargeSession_Clear();
bool chargeSession_IsLocalSession();
int8_t chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud);
void chargeSession_SetAuthenticationCode(const char * idAsString);
void chargeSession_SetParentId(const char * id_token);
char* chargeSession_GetAuthenticationCode();
void chargeSession_ClearAuthenticationCode();
//void chargeSession_SetEnergy(float energy);
void chargeSession_SetStoppedByRFID(bool stoppedByRFID, const char * id_tag);
void chargeSession_SetStoppedReason(enum ocpp_reason_id reason);
void chargeSession_SetEnergyForTesting(float e);
void chargeSession_SetOCMF(char * OCMDString);
void SetUUIDFlagAsCleared();
void chargeSession_HoldUserUUID();
char * sessionSession_GetHeldUserUUID();
bool sessionSession_IsHoldingUserUUID();
void chargeSession_ClearHeldUserUUID();

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message, size_t message_length);

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
