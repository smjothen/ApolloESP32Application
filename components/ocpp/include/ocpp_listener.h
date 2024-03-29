#ifndef OCPP_LISTENER_H
#define OCPP_LISTENER_H

#include "esp_websocket_client.h"

#include "messages/call_messages/ocpp_call_cb.h"

/** @file
 * @brief Websocket event handler for OCPP.
 *
 * Expects CS initiated .req messages and .conf replies to CP initiated messages.
 */

/**
 * @brief set a callback for a given ocpp request sent by the central system.
 *
 * @param action_id id of the action to that will initiate the callback.
 * @param call_cb the callback function to excecute when request is receive.
 * @param cb_data data to send as a parameter to the callback function.
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
 * @brief get a json object containing information about the ocpp listener state.
 */
cJSON * ocpp_listener_get_diagnostics();

/**
 * @brief events that the listener can send with xTaskNotify
 * @note Difference between disconnect and closed in the description is that disconnect is usually the result of failure.
 * in case of a disconnect the websocket_client_config_t.disable_auto_reconnect will have an effect. disable_auto_reconnect
 * will not have an effect on closed.
 */
enum ocpp_websocket_event{
	eOCPP_WEBSOCKET_CONNECTION_CHANGED = 1<<0, ///< Websocket changed to connected or disconnected
	eOCPP_WEBSOCKET_FAILURE = 1<<1, ///< A failure occured in the websocket
	eOCPP_WEBSOCKET_RECEIVED_MATCHING = 1<<2, ///< A .conf matching the active call was received
	eOCPP_WEBSOCKET_CLOSED = 1<<3, //< Websocket message sent with request to closed connection
};
#endif /*OCPP_LISTENER_H*/
