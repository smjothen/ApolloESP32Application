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


enum ChargerOperatingMode
{
	eUNKNOWN 				= 0,
	eDISCONNECTED 			= 1,
	eCONNECTED_REQUESTING 	= 2,
	eCONNECTED_CHARGING 	= 3,
	eCONNECTED_FINISHED 	= 5,
};

void sessionHandler_init();
void SetDataInterval(int newDataInterval);
void sessionHandler_ClearOfflineCurrentSent();
void sessionHandler_simulateOffline();
int sessionHandler_GetStackWatermark();
void ClearStartupSent();

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
