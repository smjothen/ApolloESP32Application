#ifndef OCPPJ_MESSAGE_STRUCTURE_H
#define OCPPJ_MESSAGE_STRUCTURE_H

#include "cJSON.h"

enum ocpp_message_type_id{
	eOCPPJ_MESSAGE_ID_CALL = 2,
	eOCPPJ_MESSAGE_ID_RESULT = 3,
	eOCPPJ_MESSAGE_ID_ERROR = 4,
};

#define OCPPJ_INDEX_MESSAGE_TYPE_ID 0
#define OCPPJ_INDEX_UNIQUE_ID 1

#define OCPPJ_CALL_INDEX_ACTION 2
#define OCPPJ_CALL_INDEX_PAYLOAD 3

#define OCPPJ_RESULT_INDEX_PAYLOAD 2

#define OCPPJ_ERROR_INDEX_ERROR_CODE 2
#define OCPPJ_ERROR_INDEX_ERROR_DESCRIPTION 3
#define OCPPJ_ERROR_INDEX_ERROR_DETAILS 3

// Errors
#define OCPPJ_ERROR_NOT_IMPLEMENTED "NotImplemented"
#define	OCPPJ_ERROR_NOT_SUPPORTED "NotSupported"
#define	OCPPJ_ERROR_INTERNAL "InternalError"
#define	OCPPJ_ERROR_PROTOCOL "ProtocolError"
#define	OCPPJ_ERROR_SECURITY "SecurityError"
#define	OCPPJ_ERROR_FORMATION_VIOLATION "FormationViolation"
#define	OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION "PropertyConstraintViolation"
#define	OCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION "OccurenceConstraintViolation"
#define	OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION "TypeConstraintViolation"
#define	OCPPJ_ERROR_GENERIC "GenericError"

enum ocppj_err_t{
	eOCPPJ_NO_ERROR, // Not part of specification: indicate success
	eOCPPJ_NO_VALUE, // Not part of specification: indicate that field optional field is not present
	eOCPPJ_ERROR_NOT_IMPLEMENTED,
	eOCPPJ_ERROR_NOT_SUPPORTED,
	eOCPPJ_ERROR_INTERNAL,
	eOCPPJ_ERROR_PROTOCOL,
	eOCPPJ_ERROR_SECURITY,
	eOCPPJ_ERROR_FORMATION_VIOLATION,
	eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION,
	eOCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION,
	eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION,
	eOCPPJ_ERROR_GENERIC,
} ;

//Actions
#define	OCPPJ_ACTION_AUTORIZE "Authorize"
#define	OCPPJ_ACTION_BOOT_NOTIFICATION "BootNotification"
#define	OCPPJ_ACTION_DATA_TRANSFER "DataTransfer"
#define	OCPPJ_ACTION_DIAGNOSTICS_STATUS_NOTIFICATION "DiagnosticsStatusNotification"
#define	OCPPJ_ACTION_FIRMWARE_STATUS_NOTIFICATION "FirmwareStatusNotification"
#define	OCPPJ_ACTION_HEARTBEAT "Heartbeat"
#define	OCPPJ_ACTION_METER_VALUES "MeterValues"
#define	OCPPJ_ACTION_START_TRANSACTION "StartTransaction"
#define	OCPPJ_ACTION_STATUS_NOTIFICATION "StatusNotification"
#define	OCPPJ_ACTION_STOP_TRANSACTION "StopTransaction"
#define	OCPPJ_ACTION_CANCEL_RESERVATION "CancelReservation"
#define	OCPPJ_ACTION_CHANGE_AVAILABILITY "ChangeAvailability"
#define	OCPPJ_ACTION_CHANGE_CONFIGURATION "ChangeConfiguration"
#define	OCPPJ_ACTION_CLEAR_CACHE "ClearCache"
#define	OCPPJ_ACTION_CLEAR_CHARGING_PROFILE "ClearChargingProfile"
#define	OCPPJ_ACTION_GET_COMPOSITE_SCHEDULE "GetCompositeSchedule"
#define	OCPPJ_ACTION_GET_CONFIGURATION "GetConfiguration"
#define	OCPPJ_ACTION_GET_DIAGNOSTICS "GetDiagnostics"
#define	OCPPJ_ACTION_GET_LOCAL_LIST_VERSION "GetLocalListVersion"
#define	OCPPJ_ACTION_REMOTE_START_TRANSACTION "RemoteStartTransaction"
#define	OCPPJ_ACTION_REMOTE_STOP_TRANSACTION "RemoteStopTransaction"
#define	OCPPJ_ACTION_RESERVE_NOW "ReserveNow"
#define	OCPPJ_ACTION_RESET "Reset"
#define	OCPPJ_ACTION_SEND_LOCAL_LIST "SendLocalList"
#define	OCPPJ_ACTION_SET_CHARGING_PROFILE "SetChargingProfile"
#define	OCPPJ_ACTION_TRIGGER_MESSAGE "TriggerMessage"
#define	OCPPJ_ACTION_UNLOCK_CONNECTOR "UnlockConnector"
#define	OCPPJ_ACTION_UPDATE_FIRMWARE "UpdateFirmware"

const char * ocppj_error_code_from_id(enum ocppj_err_t error_id);

const char * ocppj_get_string_from_message(cJSON * message, unsigned int index);

#define ocppj_get_unique_id_from_call(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})
#define ocppj_get_action_from_call(call) ({ocppj_get_string_from_message(call, OCPPJ_CALL_INDEX_ACTION);})

#define ocppj_get_unique_id_from_result(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})

#define ocppj_get_unique_id_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})
#define ocppj_get_error_code_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_ERROR_CODE);})
#define ocppj_get_error_description_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_ERROR_DESCRIPTION);})

#endif /*OCPPJ_MESSAGE_STRUCTURE_H*/
