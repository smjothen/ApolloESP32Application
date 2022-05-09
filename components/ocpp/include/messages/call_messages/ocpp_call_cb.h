#ifndef OCPP_CALL_CB_H
#define OCPP_CALL_CB_H

#include "cJSON.h"

// This enum is used to handle indexing into callback array and only relevant for
// calls initiated by the central system
enum ocpp_call_action_id{
	eOCPP_ACTION_CANCEL_RESERVATION_ID = 0,
	eOCPP_ACTION_CHANGE_AVAILABILITY_ID,
	eOCPP_ACTION_CHANGE_CONFIGURATION_ID,
	eOCPP_ACTION_CLEAR_CACHE_ID,
	eOCPP_ACTION_CLEAR_CHARGING_PROFILE_ID,
	eOCPP_ACTION_DATA_TRANSFER_ID,
	eOCPP_ACTION_GET_COMPOSITE_SCHEDULE_ID,
	eOCPP_ACTION_GET_CONFIGURATION_ID,
	eOCPP_ACTION_GET_DIAGNOSTICS_ID,
	eOCPP_ACTION_GET_LOCAL_LIST_VERSION_ID,
	eOCPP_ACTION_REMOTE_START_TRANSACTION_ID,
	eOCPP_ACTION_REMOTE_STOP_TRANSACTION_ID,
	eOCPP_ACTION_RESERVE_NOW_ID,
	eOCPP_ACTION_RESET_ID,
	eOCPP_ACTION_SEND_LOCAL_LIST_ID,
	eOCPP_ACTION_SET_CHARGING_PROFILE_ID,
	eOCPP_ACTION_TRIGGER_MESSAGE_ID,
	eOCPP_ACTION_UNLOCK_CONNECTOR_ID,
	eOCPP_ACTION_UPDATE_FIRMWARE_ID,
};

// Used to allocate space for callbacks.keep consistent with number of call_action_id entries
#define OCPP_CALL_ACTION_ID_COUNT 19

typedef void (*ocpp_call_callback) (const char * unique_id, const char * action, cJSON * payload, void * cb_data);

#endif /*OCPP_CALL_CB_H*/
