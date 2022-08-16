#ifndef OCPP_CALL_REQUEST_H
#define OCPP_CALL_REQUEST_H

#include <time.h>

#include "cJSON.h"
#include "types/ocpp_meter_value.h"
#include "ocpp_json/ocppj_message_structure.h"

cJSON * ocpp_create_authorize_request(const char * id_tag);
cJSON * ocpp_create_boot_notification_request(const char * charge_box_serial_number, const char * charge_point_model,
							const char * charge_point_serial_number, const char * charge_point_vendor, const char * firmware_version,
							const char * iccid, const char * imsi, const char * meter_serial_number, const char * meter_type);
cJSON * ocpp_create_data_transfer_request(const char * vendor_id, const char * message_id, const char * data);
cJSON * ocpp_create_diagnostics_status_notification_request(const char * status);
cJSON * ocpp_create_firmware_status_notification_request(const char * status);
cJSON * ocpp_create_heartbeat_request();

cJSON * ocpp_create_meter_values_request(unsigned int connector_id, const int * transaction_id, struct ocpp_meter_value_list * meter_values);

/**
 * use reservation_id -1 to omitt value
 * @TODO: Check if there is a value that is guarantied not to be valid or use NULL
 */
cJSON * ocpp_create_start_transaction_request(unsigned int connector_id, const char * id_tag, int meter_start, int reservation_id, time_t timestamp);
cJSON * ocpp_create_status_notification_request(unsigned int connector_id, const char * error_code, const char * info, const char * status, time_t timestamp, const char * vendor_id, const char * vendor_error_code);
cJSON * ocpp_create_stop_transaction_request(const char * id_tag, int meter_stop, time_t timestamp, int * transaction_id, const char * reason, struct ocpp_meter_value_list * transaction_data);

cJSON * ocpp_create_call(const char * action, cJSON * payload);

#endif /*OCPP_CALL_REQUEST_H*/
