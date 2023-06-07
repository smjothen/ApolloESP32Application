#ifndef OCPP_CALL_RESULT_H
#define OCPP_CALL_RESULT_H

#include <time.h>

#include "cJSON.h"

#include "types/ocpp_key_value.h"
#include "types/ocpp_charging_profile.h"

/** @file
 * @brief Contains functions for creating CP initiated CallResult.
 */

//Core profile
/**
 * @brief create a ChangeAvailability.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates whether the Charge Point is able to perform the availability change"
 */
cJSON * ocpp_create_change_availability_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a ChangeConfiguration.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. Returns whether configuration change has been accepted"
 */
cJSON * ocpp_create_change_configuration_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a ClearCache.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. Accepted if the Charge Point has executed the request, otherwise rejected."
 */
cJSON * ocpp_create_clear_cache_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a DataTransfer.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates the success or failure of the data transfer."
 * @param data "Optional. Data in response to request."
 */
cJSON * ocpp_create_data_transfer_confirmation(const char * unique_id, const char * status, const char * data);

/**
 * @brief create a GetConfiguration.conf
 *
 * @param unique_id id given in the matching .req
 * @param configuration_key "Optional. List of requested or known keys"
 * @param unknown_key_count length of unknown_key array
 * @param unknown_key "Optional. Requested keys that are unknown"
 */
cJSON * ocpp_create_get_configuration_confirmation(const char * unique_id, cJSON * configuration_key, size_t unknown_key_count, char ** unknown_key);

/**
 * @brief create a RemoteStartTransaction.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. Status indicating whether Charge Point accepts the request to start a transaction"
 */
cJSON * ocpp_create_remote_start_transaction_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a RemoteStopTransaction.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. The identifier of the transaction which Charge Point is requested to stop"
 */
cJSON * ocpp_create_remote_stop_transaction_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a Reset.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates whether the Charge Point is able to perform the reset"
 */
cJSON * ocpp_create_reset_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a UnlockConnector.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates whether the Charge Point has unlocked the connector."
 */
cJSON * ocpp_create_unlock_connector_confirmation(const char * unique_id, const char * status);

//Firmware Management Profile
/**
 * @brief create a GetDiagnostics.conf
 *
 * @param unique_id id given in the matching .req
 * @param file_name "Optional. This contains the name of the file with diagnostic information that will
 * be uploaded. This field is not present when no diagnostic information is available."
 */
cJSON * ocpp_create_get_diagnostics_confirmation(const char * unique_id, const char * file_name);

/**
 * @brief create a UpdateFirmware.conf
 *
 * @param unique_id id given in the matching .req
 */
cJSON * ocpp_create_update_firmware_confirmation(const char * unique_id);

//Local Auth List Management Profile
/**
 * @brief create a SendLocalList.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates whether the Charge Point has successfully received and applied the update of the local authorization list"
 */
cJSON * ocpp_create_send_local_list_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a GetLocalListVersion.conf
 *
 * @param unique_id id given in the matching .req
 * @param listVersion "Required. This contains the current version number of the local authorization list in the Charge Point"
 */
cJSON * ocpp_create_get_local_list_version_confirmation(const char * unique_id, int listVersion);

//Reservation Profile
/**
 * @brief create a CancelReservation.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates the success or failure of the cancelling of a reservation by Central System"
 */
cJSON * ocpp_create_cancel_reservation_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a ReserveNow.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. This indicates the success or failure of the reservation"
 */
cJSON * ocpp_create_reserve_now_confirmation(const char * unique_id, const char * status);

//Smart Charging Profile
/**
 * @brief create a ClearChargingProfile.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. Indicates if the Charge Point was able to execute the request"
 */
cJSON * ocpp_create_clear_charging_profile_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a GetCompositeSchedule.conf
 *
 * @param unique_id id given in the matching .req
 * @param status Required. "Status of the request. The Charge Point will indicate if it was able to process the request"
 * @param connector_id "Optional. The charging schedule contained in this notification applies to a Connector."
 * @param schedule_start "Optional. Time. Periods contained in the charging profile are relative to this point in time.
 * If status is "Rejected", this field may be absent."
 * @param charging_schedule "Optional. Planned Composite Charging Schedule, the energy consumption over time. Always relative to ScheduleStart.
 * If status is "Rejected", this field may be absent. "
 */
cJSON * ocpp_create_get_composite_schedule_confirmation(const char * unique_id, const char * status, int * connector_id, time_t * schedule_start, struct ocpp_charging_schedule * charging_schedule);

/**
 * @brief create a SetChargingProfile.conf
 *
 * @param unique_id id given in the matching .req
 * @param status "Required. Returns whether the Charge Point has been able to process the message successfully. This does not guarantee the schedule will be followed to
 * the letter. There might be other constraints the Charge Point may need to take into account."
 */
cJSON * ocpp_create_set_charge_profile_confirmation(const char * unique_id, const char * status);

//Remote Trigger Profile
/**
 * @brief create a TriggerMessage.conf
 *
 * @param unique_id "id given in the matching .req"
 * @param status "Required. Indicates whether the Charge Point will send the requested notification or not."
 */
cJSON * ocpp_create_trigger_message_confirmation(const char * unique_id, const char * status);

/**
 * @brief create a generic .conf
 *
 * @param unique_id id given in the matching .req
 * @param payload Payload is a JSON object containing the results of the executed Action. If there is no payload JSON
 * allows for two different notations: null or and empty object {}. Although it seems trivial we consider it good
 * practice to only use the empty object statement. Null usually represents something undefined, which is not the same as
 * empty, and also {} is shorter.
 */
cJSON * ocpp_create_call_result(const char * unique_id, cJSON * payload);
#endif /*OCPP_CALL_RESULT_H*/
