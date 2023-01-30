#ifndef OCPP_LISTENER_H
#define OCPP_LISTENER_H

#include "esp_websocket_client.h"

#include "messages/call_messages/ocpp_call_cb.h"

int attach_call_cb(enum ocpp_call_action_id action_id, ocpp_call_callback call_cb, void * cb_data);
void set_task_to_notify(TaskHandle_t);
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void clean_listener();

enum{

	eOCPP_WEBSOCKET_NO_EVENT = 0,
	eOCPP_WEBSOCKET_CONNECTED,
	eOCPP_WEBSOCKET_DISCONNECT,
	eOCPP_WEBSOCKET_FAILURE,
	eOCPP_WEBSOCKET_RECEIVED_MATCHING,

	eOCPP_FIRMWARE_UPDATE = 1<<3 // TODO: Not a listener event, consider moving field or entire enum
};
#endif /*OCPP_LISTENER_H*/
