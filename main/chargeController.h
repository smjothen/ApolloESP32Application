#ifndef _CHARGECONTROLLER_H_
#define _CHARGECONTROLLER_H_

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif



#define MONDAY 		0x01
#define TUESDAY 	0x02
#define WEDNESDAY 	0x04
#define THURSDAY 	0x08
#define FRIDAY 		0x10
#define SATURDAY 	0x20
#define SUNDAY		0x40

#define WEEKDAYS 	0x1F
#define WEEKEND		0x60


enum ChargeSource
{
	eCHARGE_SOURCE_STANDALONE 	= 1,
	eCHARGE_SOURCE_CLOUD 	  	= 2,
	eCHARGE_SOURCE_START_OFFLINE= 3,
	eCHARGE_SOURCE_GONE_OFFLINE = 4,
	eCHARGE_SOURCE_SCHEDULE	= 5,
	eCHARGE_SOURCE_NO_SCHEDULE	= 6
};

void RunStartChargeTimer();

void chargeController_WriteNewTimeSchedule(char * timeSchedule);
void chargeController_SetTimes();
bool chargeController_SendStartCommandToMCU();

void chargeController_Init();
void chargeController_Activation();
bool chargecontroller_IsPauseBySchedule();
void chargeController_SetPauseByCloudCommand(bool pauseState);

void chargeController_ClearNextStartTime();
bool chargeController_CheckForNewScheduleEvent();
char * chargeController_GetNextStartString();
bool chargeController_IsScheduleActive();

void chargeController_Override();
void chargeController_CancelOverride();
void chargeController_SetNowTime(char * timeString);
bool chargeController_SetStandaloneState(enum session_controller controller);
void chargeController_StartWithRandomDelay();
void chargeController_SetRandomStartDelay();
void chargeController_ClearRandomStartDelay();
void chargeController_SetHasBeenDisconnected();
void chargeController_SetSendScheduleDiagnosticsFlag();


#ifdef __cplusplus
}
#endif

#endif  /*_CHARGECONTROLLER_H_*/
