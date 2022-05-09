#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"

/**
 * @param current_time is the the current time after synchronization with the central system
 */
cJSON * ocpp_create_heartbeat_request(){

	return ocpp_create_call(OCPPJ_ACTION_HEARTBEAT, NULL);
}
