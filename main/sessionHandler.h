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


enum SessionResetMode
{
	eSESSION_RESET_NONE = 0,
	eSESSION_RESET_INITIATED = 1,
	eSESSION_RESET_STOP_SENT = 2,
	eSESSION_RESET_FINALIZE  = 3,
	eSESSION_RESET_DO_RESET	 = 4,
	eSESSION_RESET_WAIT	 	 = 5,
};

void sessionHandler_init();
enum ChargerOperatingMode sessionHandler_GetCurrentChargeOperatingMode();
//void SetDataInterval(int newDataInterval);
void sessionHandler_HoldParametersFromCloud(float newCurrent, int newPhases);
bool SessionHandler_IsOfflineMode();
void SessionHandler_SetOCMFHighInterval();
void SessionHandler_SetLogCurrents(int interval);
void sessionHandler_ClearCarInterfaceResetConditions();
void sessionHandler_SetStoppedByCloud(bool stateFromCloud);
int sessionHandler_GetStackWatermarkOCMF();
int sessionHandler_GetStackWatermark();
void sessionHandler_CheckAndSendOfflineSessions();
void sessionHandler_SetOfflineSessionFlag();
void ChargeModeUpdateToCloudNeeded();
void sessionHandler_SendMCUSettings();
void sessionHandler_SendRelayStates();
void sessionHandler_SendFPGAInfo();
void StackDiagnostics(bool state);
void ClearStartupSent();
void SetPendingRFIDTag(char * pendingTag);
void SetAuthorized(bool authFromCloud);
void sessionHandler_Pulse();
void sessionHandler_InitiateResetChargeSession();
void sessionHandler_StopAndResetChargeSession();
void sessionHandler_TestOfflineSessions();

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
