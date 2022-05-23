#include "esp_system.h"
#include "messages/call_messages/ocpp_call_request.h"

static char uuid[37];

//consider making rfc4122 compliant
const char * create_unique_id(){

	snprintf(uuid, sizeof(uuid), "%8x-%4x-%4x-%4x-%12x",
		esp_random(), esp_random(), esp_random(),esp_random(), esp_random());

	return uuid;
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

