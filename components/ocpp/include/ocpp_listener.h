#ifndef OCPP_LISTENER_H
#define OCPP_LISTENER_H

#include "esp_websocket_client.h"

#include "messages/call_messages/ocpp_call_cb.h"

/**
 * @brief set a callback for a given ocpp request sent by the central system.
 */
int attach_call_cb(enum ocpp_call_action_id action_id, ocpp_call_callback call_cb, void * cb_data);

/**
 * @brief Sets a task to be notified of with ocpp_websocket_event using eSetBits.
 *
 * @param task the task to be notified.
 * @param offset a value used to left shift the ocpp_websocket_event to allow room for other notifications.
 */
void ocpp_configure_websocket_notification(TaskHandle_t task, uint offset);

/**
 * @brief event handler for websocket connection to ocpp central system.
 */
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**
 * @brief free listener variables
 */
void clean_listener();

/**
 * @brief True if websocket connection to central system is available.
 */
bool ocpp_is_connected();

enum ocpp_websocket_event{
	eOCPP_WEBSOCKET_CONNECTION_CHANGED = 1<<0, // Websocket connected or disconnected
	eOCPP_WEBSOCKET_FAILURE = 1<<1,
	eOCPP_WEBSOCKET_RECEIVED_MATCHING = 1<<2,
};
#endif /*OCPP_LISTENER_H*/
