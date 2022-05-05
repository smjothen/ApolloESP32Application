#ifndef _SESSIONHANDLER__H_
#define _SESSIONHANDLER__H_

#ifdef __cplusplus
extern "C" {
#endif


enum CarChargeMode
{
	eCAR_UNINITIALIZED 	= 0xFF,
	eCAR_DISCONNECTED 	= 12,
	eCAR_CONNECTED 		= 9,
	eCAR_CHARGING 		= 6,
	eCAR_STATE_F 		= -12,
};


/*enum ChargerOperatingMode
{
	eUNKNOWN 				= 0,
	eDISCONNECTED 			= 1,
	eCONNECTED_REQUESTING 	= 2,
	eCONNECTED_CHARGING 	= 3,
	eCONNECTED_FINISHED 	= 5,
};*/

enum ChargerOperatingMode
{
	CHARGE_OPERATION_STATE_UNINITIALIZED    = 0,
    CHARGE_OPERATION_STATE_DISCONNECTED     = 1,
    CHARGE_OPERATION_STATE_REQUESTING       = 2,
    CHARGE_OPERATION_STATE_ACTIVE           = 10,
    CHARGE_OPERATION_STATE_CHARGING         = 3,
    CHARGE_OPERATION_STATE_STOPPING         = 4,
    CHARGE_OPERATION_STATE_PAUSED           = 5,
    CHARGE_OPERATION_STATE_STOPPED          = 6,
    CHARGE_OPERATION_STATE_WARNING          = 7,
};


void sessionHandler_init();
enum ChargerOperatingMode sessionHandler_GetCurrentChargeOperatingMode();
//void SetDataInterval(int newDataInterval);
void sessionHandler_HoldParametersFromCloud(float newCurrent, int newPhases);
bool SessionHandler_IsOfflineMode();
void SessionHandler_SetOCMFHighInterval();
void SessionHandler_SetLogCurrents();
void sessionHandler_ClearCarInterfaceResetConditions();
void sessionHandler_SetStoppedByCloud(bool stateFromCloud);
int sessionHandler_GetStackWatermarkOCMF();
int sessionHandler_GetStackWatermark();
void sessionHandler_CheckAndSendOfflineSessions();
void sessionHandler_SetOfflineSessionFlag();
void ChargeModeUpdateToCloudNeeded();
void StackDiagnostics(bool state);
void ClearStartupSent();
void SetPendingRFIDTag(char * pendingTag);
void SetAuthorized(bool authFromCloud);
void sessionHandler_Pulse();

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
