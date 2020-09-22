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
};

void chargeSession_Start();
void chargeSession_End();
void chargeSession_SetAuthenticationCode(char * idAsString);
void chargeSession_SetEnergy(float energy);

struct ChargeSession chargeSession_Get();
int chargeSession_GetSessionAsString(char * message);

#ifdef __cplusplus
}
#endif

#endif  /*_CHARGESESSION_H_*/
