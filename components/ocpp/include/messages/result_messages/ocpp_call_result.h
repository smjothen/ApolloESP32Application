#ifndef OCPP_CALL_RESULT_H
#define OCPP_CALL_RESULT_H

#include <time.h>

#include "cJSON.h"

#include "types/ocpp_key_value.h"
#include "types/ocpp_charging_profile.h"

//Core profile
cJSON * ocpp_create_change_availability_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_change_configuration_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_clear_cache_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_data_transfer_confirmation(const char * unique_id, const char * status, const char * data);
cJSON * ocpp_create_get_configuration_confirmation(const char * unique_id, size_t configuration_key_count, struct ocpp_key_value * configuration_key, size_t unknown_key_count, char ** unknown_key);
cJSON * ocpp_create_remote_start_transaction_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_remote_stop_transaction_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_reset_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_unlock_connector_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_update_firmware_confirmation(const char * unique_id);

//Firmware Management Profile
cJSON * ocpp_create_get_diagnostics_confirmation(const char * unique_id, const char * file_name);
cJSON * ocpp_create_update_firmware_confirmation(const char * unique_id);

//Local Auth List Management Profile
cJSON * ocpp_create_send_local_list_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_get_local_list_version_confirmation(const char * unique_id, int listVersion);

//Reservation Profile
cJSON * ocpp_create_cancel_reservation_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_reserve_now_confirmation(const char * unique_id, const char * status);

//Smart Charging Profile
cJSON * ocpp_create_clear_charging_profile_confirmation(const char * unique_id, const char * status);
cJSON * ocpp_create_get_composite_schedule_confirmation(const char * unique_id, const char * status, int * connector_id, time_t * schedule_start, struct ocpp_charging_schedule * charging_schedule);
cJSON * ocpp_create_set_charge_profile_confirmation(const char * unique_id, const char * status);

//Remote Trigger Profile
cJSON * ocpp_create_trigger_message_confirmation(const char * unique_id, const char * status);

cJSON * ocpp_create_call_result(const char * unique_id, cJSON * payload);
#endif /*OCPP_CALL_RESULT_H*/
