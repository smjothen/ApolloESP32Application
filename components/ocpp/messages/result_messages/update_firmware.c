#include "messages/result_messages/ocpp_call_result.h"

cJSON * ocpp_create_update_firmware_confirmation(const char * unique_id){

	return ocpp_create_call_result(unique_id, NULL);
}
