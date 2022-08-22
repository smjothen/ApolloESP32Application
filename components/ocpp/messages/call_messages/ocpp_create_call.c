#include "esp_system.h"
#include "messages/call_messages/ocpp_call_request.h"

static char uuid_str[37];

struct uuid{
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_high_version;
	uint8_t clock_seq_high_reserved;
	uint8_t clock_seq_low;
	uint8_t node[6];
};

static const char * create_unique_id(){
	struct uuid id;

	esp_fill_random(&id, sizeof(struct uuid));

	id.clock_seq_high_reserved |= 0b10000000;
	id.clock_seq_high_reserved &= 0b10111111;

	id.time_high_version |= 0b0100000000000000;
	id.time_high_version &= 0b0100111111111111;

	snprintf(uuid_str, sizeof(uuid_str), "%2.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
		id.time_low, id.time_mid, id.time_high_version, id.clock_seq_high_reserved, id.clock_seq_low,
		id.node[0], id.node[1], id.node[2], id.node[3], id.node[4], id.node[5]
		);

	return uuid_str;
}

/**
 * @brief sends a ocpp call request on the given websocket.
 * @return Unique id of the request or -1 on error.
 */
cJSON * ocpp_create_call(const char * action, cJSON * payload){

	cJSON * message = cJSON_CreateArray();
	if(message == NULL){
		return NULL;
	}

	cJSON * message_type_id = cJSON_CreateNumber(eOCPPJ_MESSAGE_ID_CALL);
	if(message_type_id == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, message_type_id);

	cJSON * unique_id_json = cJSON_CreateString(create_unique_id());
	if(unique_id_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, unique_id_json);

	cJSON * action_json = cJSON_CreateString(action);
	if(action_json == NULL){
		goto error;
	}
	cJSON_AddItemToArray(message, action_json);

	if(payload == NULL){
		cJSON * payload_local = cJSON_CreateObject();
		if(payload_local == NULL){
			goto error;
		}
		cJSON_AddItemToArray(message, payload_local);

	}else{
		cJSON_AddItemToArray(message, payload);
	}

	return message;

error:
	cJSON_Delete(message);
	return NULL;
}

