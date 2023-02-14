#ifndef OCPP_CALL_CB_H
#define OCPP_CALL_CB_H

#include "cJSON.h"

/** @file
 * @brief Contains info to handle CallResult for a CS initiated .req
 */

/**
 * @brief Id used to identify which action a callback relates to
 */
enum ocpp_call_action_id{
	eOCPP_ACTION_CANCEL_RESERVATION_ID = 0, ///< CancelReservation.req
	eOCPP_ACTION_CHANGE_AVAILABILITY_ID, ///< ChangeAvailability.req
	eOCPP_ACTION_CHANGE_CONFIGURATION_ID, ///< ChangeConfiguration.req
	eOCPP_ACTION_CLEAR_CACHE_ID, ///< ClearCache.req
	eOCPP_ACTION_CLEAR_CHARGING_PROFILE_ID, ///< ClearChargingProfile.req
	eOCPP_ACTION_DATA_TRANSFER_ID, ///< DataTransfer.req
	eOCPP_ACTION_GET_COMPOSITE_SCHEDULE_ID, ///< GetCompositeSchedule.req
	eOCPP_ACTION_GET_CONFIGURATION_ID, ///< GetConfiguration.req
	eOCPP_ACTION_GET_DIAGNOSTICS_ID, ///< GetDiagnostics.req
	eOCPP_ACTION_GET_LOCAL_LIST_VERSION_ID, ///< GetLocalListVersion.req
	eOCPP_ACTION_REMOTE_START_TRANSACTION_ID, ///< RemoteStartTransaction.req
	eOCPP_ACTION_REMOTE_STOP_TRANSACTION_ID, ///< RemoteStopTransaction.req
	eOCPP_ACTION_RESERVE_NOW_ID, ///< ReserveNow.req
	eOCPP_ACTION_RESET_ID, ///< Reset.req
	eOCPP_ACTION_SEND_LOCAL_LIST_ID, ///< SendLocalList.req
	eOCPP_ACTION_SET_CHARGING_PROFILE_ID, ///< SetChargingProfile.req
	eOCPP_ACTION_TRIGGER_MESSAGE_ID, ///< TriggerMessage.req
	eOCPP_ACTION_UNLOCK_CONNECTOR_ID, ///< UnlockConnector.req
	eOCPP_ACTION_UPDATE_FIRMWARE_ID, ///< UpdateFirmware.req
};

/// Number of entries in ocpp_call_action_id. Used to allocate space for callbacks.
#define OCPP_CALL_ACTION_ID_COUNT 19

/**
 * @brief Callback for CallResult
 *
 * @param unique_id "this is a unique identifier that will be used to match request and result."
 * @param action "the name of the remote procedure or action. This will be a case-sensitive string containing the same
 * value as the Action-field in SOAP-based messages, without the preceding slash."
 * @param payload "Payload is a JSON object containing the arguments relevant to the Action. If there is no payload JSON
 * allows for two different notations: null or and empty object {}. Although it seems trivial we consider it good practice to only use the empty
 * object statement. Null usually represents something undefined, which is not the same as empty, and also {} is shorter."
 * @param cb_data Additional data not defined in ocpp
 */
typedef void (*ocpp_call_callback) (const char * unique_id, const char * action, cJSON * payload, void * cb_data);

#endif /*OCPP_CALL_CB_H*/
