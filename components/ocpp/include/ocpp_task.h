#ifndef OCPP_TASK_H
#define OCPP_TASK_H

#include <stdbool.h>

#include "esp_websocket_client.h"

#include "ocpp_call_with_cb.h"
#include "types/ocpp_registration_status.h"

#define OCPP_CALL_TIMEOUT 10000

/**
 * Determins queueing of outgoing calls and error handling in case of transactions.
 */
enum call_type{
	eOCPP_CALL_GENERIC = 0,
	eOCPP_CALL_TRANSACTION_RELATED,
	eOCPP_CALL_BLOCKING
};

/**
 * Reply to central service originated action call.
 */
int send_call_reply(cJSON * call);

/**
 * Used to send new action calls originating from the charge point once all prior calls have finished
 */
int enqueue_call(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type);

int start_ocpp(const char * charger_id, uint32_t ocpp_heartbeat_interval, uint8_t ocpp_transaction_message_attempts, uint16_t ocpp_transaction_message_retry_interval);
void stop_ocpp(void);

/**
 * Get the stucture with the message that was last transmitted to the central system and expecting a reply.
 */
struct ocpp_call_with_cb * get_active_call();

/**
 * Indicate that a reply has been recieved and handled or failed and clear stucture for next ocpp call.
 * This function is called by ocpp_listener.
 */
void clear_active_call(void);
const char * get_active_call_id(void);

void handle_ocpp_call(void);

int complete_boot_notification_process(char * serial_nr);

int start_ocpp_heartbeat(void);
void stop_ocpp_heartbeat(void);
void update_heartbeat_timer(uint sec);

void update_transaction_message_related_config(uint8_t ocpp_transaction_message_attempts, uint16_t ocpp_transaction_message_retry_interval);

/**
 * Check if weboscket is connected to central system
 */
bool is_connected(void);
void set_connected(bool connected);

enum ocpp_registration_status get_registration_status(void);

#endif /* OCPP_TASK_H */
