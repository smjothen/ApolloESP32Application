#ifndef OCPPJ_MESSAGE_STRUCTURE_H
#define OCPPJ_MESSAGE_STRUCTURE_H

#include "cJSON.h"

/** @file
 * @brief Contains information specific to the OCPP json protocol.
 */

/**
 * @brief "To identify the type of message one of the following Message Type Numbers MUST be used."
 */
enum ocpp_message_type_id{
	eOCPPJ_MESSAGE_ID_CALL = 2, ///< Client-to-Server
	eOCPPJ_MESSAGE_ID_RESULT = 3, ///< Server-to-Client
	eOCPPJ_MESSAGE_ID_ERROR = 4, ///< Server-to-Client
};

#define OCPPJ_INDEX_MESSAGE_TYPE_ID 0 ///< Index of MessageTypeId in any ocpp message array
#define OCPPJ_INDEX_UNIQUE_ID 1 ///< Index of UniqueId in any ocpp message array

#define OCPPJ_CALL_INDEX_ACTION 2 ///< Index of Action name in a Call message array
#define OCPPJ_CALL_INDEX_PAYLOAD 3 ///< Index of Payload data in a Call message array

#define OCPPJ_RESULT_INDEX_PAYLOAD 2 ///< Index of Payload data in a CallResult message array

#define OCPPJ_ERROR_INDEX_ERROR_CODE 2 ///< Index of ErrorCode in a CallError message array
#define OCPPJ_ERROR_INDEX_ERROR_DESCRIPTION 3 ///< Index of ErrorDescription in a CallError message array
#define OCPPJ_ERROR_INDEX_ERROR_DETAILS 3 ///< Index of ErrorDetails data in a CallError message array

/** @name ErrorCode
 * @brief ErrorCode strings used in CallError to identify type of error
 */
///@{
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
///@}

/**
 * @brief Identifies the different error codes specified in OCPP.
 */
enum ocppj_err_t{
	eOCPPJ_NO_ERROR, ///< Not part of specification: indicate success while parsing
	eOCPPJ_NO_VALUE, ///< Not part of specification: indicate that optional field is not present while parsing
	eOCPPJ_ERROR_NOT_IMPLEMENTED, ///< "Requested Action is not known by receiver"
	eOCPPJ_ERROR_NOT_SUPPORTED, ///< "Requested Action is recognized but not supported by the receiver"
	eOCPPJ_ERROR_INTERNAL, ///< "An internal error occurred and the receiver was not able to process the requested Action successfully"
	eOCPPJ_ERROR_PROTOCOL, ///< "Payload for Action is incomplete"
	eOCPPJ_ERROR_SECURITY, ///< "During the processing of Action a security issue occurred preventing receiver from completing the Action successfully"
	eOCPPJ_ERROR_FORMATION_VIOLATION, ///< "Payload for Action is syntactically incorrect or not conform the PDU structure for Action"
	eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, ///< "Payload is syntactically correct but at least one field contains an invalid value"
	eOCPPJ_ERROR_OCCURENCE_CONSTRAINT_VIOLATION, ///< "Payload for Action is syntactically correct but at least one of the fields violates occurence constraints"
	eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION,  ///< "Payload for Action is syntactically correct but at least one of the fields violates data type constraints (e.g. “somestring”: 12)"
	eOCPPJ_ERROR_GENERIC, ///< "Any other error not covered by the previous ones"
} ;

/** @name Actions
 * @brief Action names are used to identify the remote procedure
 */
///@{
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
///@}

/**
 * @brief Converts an error code id to the string expected in the CallError JSON.
 *
 * @param error_id the id to convert.
 */
const char * ocppj_error_code_from_id(enum ocppj_err_t error_id);

/**
 * @brief gets a string from the json message array
 *
 * @param message the JSON message
 * @param index the array index where a string is expected.
 */
const char * ocppj_get_string_from_message(cJSON * message, unsigned int index);

/**
 * @brief Gets the unique id from a Call
 *
 * @param call the Call
 */
#define ocppj_get_unique_id_from_call(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})

/**
 * @brief Gets the action name form a Call
 *
 * @param call the Call
 */
#define ocppj_get_action_from_call(call) ({ocppj_get_string_from_message(call, OCPPJ_CALL_INDEX_ACTION);})

/**
 * @brief Gets unique id from CallResult
 *
 * @param call the Call
 */
#define ocppj_get_unique_id_from_result(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})


/**
 * @brief Gets unique id from CallError
 *
 * @param call the Call
 */
#define ocppj_get_unique_id_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_UNIQUE_ID);})

/**
 * @brief Gets ErrorCode from CallError
 *
 * @param call the Call
 */
#define ocppj_get_error_code_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_ERROR_CODE);})

/**
 * @brief Gets ErrorDescription from CallError
 *
 * @param call the Call
 */
#define ocppj_get_error_description_from_error(call) ({ocppj_get_string_from_message(call, OCPPJ_INDEX_ERROR_DESCRIPTION);})

#endif /*OCPPJ_MESSAGE_STRUCTURE_H*/
