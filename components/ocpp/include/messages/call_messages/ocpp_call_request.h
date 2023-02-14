#ifndef OCPP_CALL_REQUEST_H
#define OCPP_CALL_REQUEST_H

#include <time.h>

#include "cJSON.h"
#include "types/ocpp_meter_value.h"
#include "ocpp_json/ocppj_message_structure.h"

/** @file
 * @brief Contains functions for creating CP initiated .req messages
 */

/**
 * @brief create a Authorize.req
 *
 * @param id_tag "Required. This contains the identifier that needs to be authorized."
 */
cJSON * ocpp_create_authorize_request(const char * id_tag);

/**
 * @brief create a BootNotification.req
 *
 * @param charge_box_serial_number "Optional. This contains a value that identifies the serial number of
 * the Charge Box inside the Charge Point. Deprecated, will be removed in future version"
 * @param charge_point_model "Required. This contains a value that identifies the model of the ChargePoint"
 * @param charge_point_serial_number "Optional. This contains a value that identifies the serial number of the Charge Point"
 * @param charge_point_vendor "Required. This contains a value that identifies the vendor of the ChargePoint."
 * @param firmware_version "Optional. This contains the firmware version of the Charge Point"
 * @param iccid "Optional. This contains the ICCID of the modem’s SIM card"
 * @param imsi "Optional. This contains the IMSI of the modem’s SIM card"
 * @param meter_serial_number "Optional. This contains the serial number of the main electrical meter of the Charge Point."
 * @param meter_type "Optional. This contains the type of the main electrical meter of the Charge Point."
 */
cJSON * ocpp_create_boot_notification_request(const char * charge_box_serial_number, const char * charge_point_model,
							const char * charge_point_serial_number, const char * charge_point_vendor, const char * firmware_version,
							const char * iccid, const char * imsi, const char * meter_serial_number, const char * meter_type);
/**
 * @brief create a DataTransfer.req
 *
 * @param vendor_id "Required. This identifies the Vendor specific implementation"
 * @param message_id "Optional. Additional identification field"
 * @param data "Optional. Data without specified length or format"
 */
cJSON * ocpp_create_data_transfer_request(const char * vendor_id, const char * message_id, const char * data);

/**
 * @brief create a DiagnosticsStatusNotification.req
 *
 * @param status "Required. This contains the status of the diagnostics upload."
 */
cJSON * ocpp_create_diagnostics_status_notification_request(const char * status);

/**
 * @brief create a FirmwareStatusNotification.req
 *
 * @param status "Required. This contains the progress status of the firmware installation."
 */
cJSON * ocpp_create_firmware_status_notification_request(const char * status);

/**
 * @brief create a Heartbeat.req
 */
cJSON * ocpp_create_heartbeat_request();

/**
 * @brief create a MeterValues.req
 *
 * @param connector_id "Required. This contains a number (>0) designating a connector of the Charge Point.‘0’ (zero) is used to designate the main powermeter"
 * @param transaction_id "Optional. The transaction to which these meter samples are related"
 * @param meter_values "Required. The sampled meter values with timestamps."
 */
cJSON * ocpp_create_meter_values_request(unsigned int connector_id, const int * transaction_id, struct ocpp_meter_value_list * meter_values);

/**
 * @brief create a StartTransaction.req
 *
 * @param connector_id "Required. This identifies which connector of the Charge Point is used"
 * @param id_tag "Required. This contains the identifier for which a transaction has to be started"
 * @param meter_start "Required. This contains the meter value in Wh for the connector at start of the transaction"
 * @param reservation_id "Optional. This contains the id of the reservation that terminates as a result of this transaction"
 * @param timestamp "Required. This contains the date and time on which the transaction is started"
 */
cJSON * ocpp_create_start_transaction_request(unsigned int connector_id, const char * id_tag, int meter_start, int * reservation_id, time_t timestamp);

/**
 * @brief create a StatusNotification.req
 *
 * @param connector_id "Required. The id of the connector for which the status is reported. Id '0' (zero) is used if the status is for the Charge Point main controller."
 * @param error_code "Required. This contains the error code reported by the Charge Point."
 * @param info "Optional. Additional free format information related to the error."
 * @param status "Required. This contains the current status of the Charge Point."
 * @param timestamp "Optional. The time for which the status is reported. If absent time of receipt of the message will be assumed."
 * @param vendor_id "Optional. This identifies the vendor-specific implementation."
 * @param vendor_error_code "Optional. This contains the vendor-specific error code."
 */
cJSON * ocpp_create_status_notification_request(unsigned int connector_id, const char * error_code, const char * info, const char * status, time_t timestamp, const char * vendor_id, const char * vendor_error_code);

/**
 * @brief create a StopTransaction.req
 *
 * @param id_tag "Optional. This contains the identifier which requested to stop the charging. It is optional because a Charge Point may terminate charging without the presence
 * of an idTag, e.g. in case of a reset. A Charge Point SHALL send the idTag if known."
 * @param meter_stop "Required. This contains the meter value in Wh for the connector at end of the transaction."
 * @param timestamp "Required. This contains the date and time on which the transaction is stopped."
 * @param transaction_id "Required. This contains the transaction-id as received by the StartTransaction.conf."
 * @param reason "Optional. This contains the reason why the transaction was stopped. MAY only be omitted when the Reason is "Local"."
 * @param transaction_data "Optional. This contains transaction usage details relevant for billing purposes."
 */
cJSON * ocpp_create_stop_transaction_request(const char * id_tag, int meter_stop, time_t timestamp, int * transaction_id, const char * reason, struct ocpp_meter_value_list * transaction_data);

/**
 * @brief create a generic .req
 *
 * @param action "the name of the remote procedure or action. This will be a case-sensitive string containing the same
 * value as the Action-field in SOAP-based messages, without the preceding slash."
 * @param payload "Payload is a JSON object containing the arguments relevant to the Action. If there is no payload JSON allows for two different notations: null or and
 * empty object {}. Although it seems trivial we consider it good practice to only use the empty object statement. Null usually represents
 * something undefined, which is not the same as empty, and also {} is shorter."
 */
cJSON * ocpp_create_call(const char * action, cJSON * payload);

#endif /*OCPP_CALL_REQUEST_H*/
