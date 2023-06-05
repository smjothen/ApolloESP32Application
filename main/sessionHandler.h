#ifndef _SESSIONHANDLER__H_
#define _SESSIONHANDLER__H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../components/ocpp/include/types/ocpp_meter_value.h"
#include "../components/ocpp/include/types/ocpp_reason.h"

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
void SetMemoryDiagnosticsFrequency(uint16_t freq);
void SetMCUDiagnosticsFrequency(uint16_t freq);
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
bool sessionHandler_OcppTransactionIsActive(uint connector_id);
int * sessionHandler_OcppGetTransactionId(uint connector_id, bool * valid_out);
time_t sessionHandler_OcppTransactionStartTime();
void sessionHandler_OcppSetChargingVariables(float min_charging_limit, float max_charging_limit, uint8_t number_phases);
void sessionHandler_OcppStopTransaction(enum ocpp_reason_id reason);
// Transfers the ownership of the meter values to sessionHandler
void sessionHandler_OcppTransferMeterValues(uint connector_id, struct ocpp_meter_value_list * values, size_t length);
void sessionHandler_OcppSaveState();
bool sessionHandler_OcppStateHasChanged();
void sessionHandler_OcppSendState(bool is_trigger);
void ChargeModeUpdateToCloudNeeded();
void sessionHandler_SendMCUSettings();
void sessionHandler_SendRelayStates();
void sessionHandler_SendFPGAInfo();

void sessionHandler_SendMIDStatus(void);
void sessionHandler_SendMIDStatusUpdate(void);
	
void StackDiagnostics(bool state);
void ClearStartupSent();
void SetPendingRFIDTag(const char * pendingTag);
void SetAuthorized(bool authFromCloud);
void sessionHandler_Pulse();
void sessionHandler_InitiateResetChargeSession();
void sessionHandler_StopAndResetChargeSession();
void sessionHandler_TestOfflineSessions();

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
