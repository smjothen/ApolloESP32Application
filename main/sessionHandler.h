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
//void SetDataInterval(int newDataInterval);
void sessionHandler_HoldParametersFromCloud(float newCurrent, int newPhases);
void sessionHandler_SetStoppedByCloud(bool stateFromCloud);
void sessionHandler_ClearOfflineCurrentSent();
void sessionHandler_simulateOffline(int offlineTime);
int sessionHandler_GetStackWatermark();
void ClearStartupSent();
void SetPendingRFIDTag(char * pendingTag);
void SetAuthorized(bool authFromCloud);

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
