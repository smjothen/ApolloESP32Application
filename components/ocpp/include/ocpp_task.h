#ifndef OCPP_TASK_H
#define OCPP_TASK_H

#include <stdbool.h>

#include "esp_websocket_client.h"

#include "ocpp_json/ocppj_message_structure.h"
#include "ocpp_call_with_cb.h"
#include "types/ocpp_registration_status.h"
#include "types/ocpp_charge_point_status.h"

/** @file
 * @brief Main file for the OCPP component.
 *
 * Handles websocket creation, registration with the CS, heatbeats, sending messages and synchronization of CS/CP communication.
 */

/**
 * @brief configuration struct containing read/write values that could be persisted through boot
 */
struct ocpp_client_config{
	const char * url;
	const char * cbid;
	const char * authorization_key;
	uint32_t heartbeat_interval;
	uint8_t transaction_message_attempts;
	uint16_t transaction_message_retry_interval;
	uint32_t websocket_ping_interval;
	uint8_t security_profile;
};

/**
 * @brief changes the maximum time before a call is considered timed out.
 *
 * @param timeout the new maximum duration of a call
 */
void ocpp_change_message_timeout(uint16_t timeout);

/**
 * @brief changes the ping interval for the ocpp websocket
 *
 * @param ping_interval the new interval in seconds used by the websocket. 0 is not treated as "no ping"
 */
void ocpp_change_websocket_ping_interval(uint32_t ping_interval);

/**
 * @brief changes the minimum delay for certain StatusNotification.req calls
 *
 * @details This function is meant for implementation of MinimumStatusDuration described in OCPP spec section 9.1.20.
 * The duration given is the delay from a notification sent with ocpp_send_status_notification with important set to false,
 * to the notification is enqueued to be sent with handle_ocpp_call if there has not been any other call to
 * ocpp_send_status_notification. If another call to ocpp_send_status_notification is made, then the new call will replace
 * the old one.
 *
 * @param duration time to wait for a replacing status notification to prevent sending the current notification
 */
void ocpp_change_minimum_status_duration(uint32_t duration);

/**
 * @brief Determins queueing of outgoing calls and error handling in case of transactions.
 */
enum call_type{
	eOCPP_CALL_GENERIC = 1<<0, ///< Most ocpp messages
	eOCPP_CALL_TRANSACTION_RELATED = 1<<1, ///< Transaction related messages like Start/StopTransaction.req and MeterValues.req
	eOCPP_CALL_BLOCKING = 1<<2, ///< Messages like BootNotification.req that must be prioritized as they may prevent other messages.
};

/**
 * @brief Reply to central service originated action call.
 *
 * @param call message to send to CS
 */
int send_call_reply(cJSON * call);

/**
 * @brief send a status notification to CS
 *
 * @param new_state "Required. This contains the current status of the Charge Point."
 * @param error_code "Required. This contains the error code reported by the Charge Point."
 * @param info "Optional. Additional free format information related to the error."
 * @param vendor_id "Optional. This identifies the vendor-specific implementation."
 * @param vendor_error_code "Optional. This contains the vendor-specific error code."
 * @param important if false the notification will wait for MinimumStatusDuration seconds and
 * not be sent if new notification is created within the duration.
 * @param is_trigger should be true if initiated by a TriggerMessage.req
 *
 * @note is_trigger is ignored if important is false.
 */
void ocpp_send_status_notification(enum ocpp_cp_status_id new_state, const char * error_code, const char * info,
				const char * vendor_id, const char * vendor_error_code, bool important, bool is_trigger);

/**
 * @brief Prepares a CP initiated call (.req call)
 *
 * @param call message to be sent.
 * @param result_cb function to be called if CS responds that request has been accepted.
 * @param error_cb function to be called if CS responds that the request has been rejected.
 * @param cb_data data to send to be given as a parameter to the response callback.
 * @param type Indicates the priority and error handling of the call
 */
int enqueue_call(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type);

/**
 * @brief Equivalent to enqueue_call, but uses a wait of '0' when waiting for semaphores or other thread synchronisation.
 * Allows enqueuing calls from rtos timers.
 *
 * @see enqueue_call
 */
int enqueue_call_immediate(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type);

/**
 * @brief Equivalent to enqueue_call*, but call is treated as the result of a TriggerMessage.req.
 *
 * @details Trigger messages are treated differently than normal calls as they may be sent without CP being accepted by CS.
 * OCPP errata v4.0 recommend that trigger messages request for BootNotification.req is rejected if CP is already acceoted.
 *
 * @see enqueue_call
 */
int enqueue_trigger(cJSON * call, ocpp_result_callback result_cb, ocpp_error_callback error_cb, void * cb_data, enum call_type type, bool immediate);

/**
 * @brief Prevent calls from being enqueued using enqueue_call and similar functions.
 *
 * @param call_type_mask a mask containing call_type to identify which types should be prevented.
 * @see call_type
 */
void block_enqueue_call(uint8_t call_type_mask);

/**
 * @brief gets mask containing call_type that currently prevent enqueuing calls.
 * @see call_type
 */
uint8_t get_blocked_enqueue_mask();

/**
 * @brief gets the number of .req calls currently waiting to be sent.
 */
size_t enqueued_call_count();

/**
 * @brief True if websocket connection to central system is available.
 */
bool ocpp_is_connected();

/**
 * @brief starts ocpp connection with CS
 *
 * @param ocpp_config structure containing read/write values persisted through boot
 */
esp_err_t start_ocpp(struct ocpp_client_config * ocpp_config);

/**
 * @brief stops ocpp connection with CS and cleans up resources.
 */
void stop_ocpp(void);

/**
 * @brief A call that has been prepared or sent to CS.
 *
 * We store the original timestamp and retries with the active_call. These should only be relevant for
 * failed transactions, but makes it easier to identify failed transaction during retransmit.
 */
struct ocpp_active_call{
	struct ocpp_call_with_cb * call; ///< .req call has been prepared or sent
	bool is_transaction_related; ///< if transaction related message
	time_t timestamp; ///< When the message was first sent
	uint retries; ///< How many retries left before the call fails.
};

/**
 * @brief Compare and handles the active call with a result preventing race conditions for send/recieve and loaded/failed transactions.
 *
 * @param unique_id id to compare active call against.
 * @param message_type type of response. Expecting CallResult or CallError.
 * @param payload .conf data to be sent to the callback active call payload.
 * @param error_code error code in case of error response.
 * @param error_description error description in case of error response.
 * @param error_details error details on case of error response.
 * @param timeout_ms max wait when attemting to read the active call.
 */
BaseType_t handle_active_call_if_match(const char * unique_id, enum ocpp_message_type_id message_type, cJSON * payload, char * error_code, char * error_description, cJSON * error_details, uint timeout_ms);

/**
 * @brief Fails the active call and handle retry in case it is transaction related.
 *
 * @param call The call that should fail.
 * @param error_code the error code to send to the callback function if it should not be retried
 * @param error_description the error description to send to the callback function if it should not be retried
 * @param error_details the error details to send to the callback function if it should not be retried
 */
void fail_active_call(struct ocpp_active_call * call, const char * error_code, const char * error_description, cJSON * error_details);

/**
 * @brief calls fail_active_call with timeout description for active call
 */
void timeout_active_call();

/**
 * @brief Sends the next message of type CALL (.req) if any exists
 *
 * @param remaining_call_count_out number of enqueued messages or positive number if transactions exist on file.
 *
 * @return ESP_OK if a message was sent. ESP_ERR_NOT_FOUND if no message was available to be sent. ESP_FAIL otherwise.
 */
esp_err_t handle_ocpp_call(int * remaining_call_count_out);

/**
 * @brief blocks dequeueing and sending calls when using handle_ocpp_call.
 *
 * @param call_type_mask mask containing call_type to indicate which type of calls  should be blocked.
 */
void block_sending_call(uint8_t call_type_mask);

/**
 * @brief Sets a task to be notified of ocpp_task_event using eSetBits.
 *
 * @param task the task to be notified.
 * @param offset a value used to left shift the ocpp_task_event to allow room for other notifications.
 */
void ocpp_configure_task_notification(TaskHandle_t task, uint offset);

/**
 * @brief Used to register with the CS using bootNotification.req.
 *
 * @param charge_box_serial_number "Optional. This contains a value that identifies the serial number of
 * the Charge Box inside the Charge Point. Deprecated, will be removed in future version"
 * @param charge_point_model "Required. This contains a value that identifies the model of the ChargePoint"
 * @param charge_point_serial_number "Optional. This contains a value that identifies the serial number of
 * the Charge Point"
 * @param charge_point_vendor "Required. This contains a value that identifies the vendor of the ChargePoint"
 * @param firmware_version "Optional. This contains the firmware version of the Charge Point"
 * @param iccid "Optional. This contains the ICCID of the modem’s SIM card"
 * @param imsi "Optional. This contains the IMSI of the modem’s SIM card"
 * @param meter_serial_number "Optional. This contains the serial number of the main electrical
 * meter of the Charge Point"
 * @param meter_type "Optional. This contains the type of the main electrical meter of the Charge Point"
 *
 * @note should only be called by a task set with ocpp_configure_task_notification
 */
int complete_boot_notification_process(const char * charge_box_serial_number, const char * charge_point_model,
				const char * charge_point_serial_number, const char * charge_point_vendor,
				const char * firmware_version, const char * iccid, const char * imsi,
				const char * meter_serial_number, const char * meter_type);

/**
 * @brief Used by complete_boot_notification_process and when requested with trigger message.
 *
 * @param is_trigger Should be true if initiated by a TriggerMessage.req
 */
int enqueue_boot_notification(bool is_trigger);

/**
 * @brief starts ocpp heartbeat
 */
int start_ocpp_heartbeat(void);

/**
 * @brief used to respond to a triggerMessage.req triggering a ocpp_heartbeat
 */
void ocpp_trigger_heartbeat();

/**
 * @brief stop ocpp heartbeat on interval
 */
void stop_ocpp_heartbeat(void);

/**
 * @brief changes the interval between ocpp heartbeats
 *
 * @param sec Interval in seconds between each heartbeat request.
 */
void update_heartbeat_timer(uint sec);

/**
 * @brief change number of retries and interval between each failed transaction related message
 *
 * @param ocpp_transaction_message_attempts number of retries before giving up on a transaction message.
 * @param ocpp_transaction_message_retry_interval delay between each attempt. Will be multiplied by number of failed attempts.
 */
void update_transaction_message_related_config(uint8_t ocpp_transaction_message_attempts, uint16_t ocpp_transaction_message_retry_interval);

/**
 * @brief get a json object containing information about the ocpp task state.
 */
cJSON * ocpp_task_get_diagnostics();

/**
 * @brief gets the latest result of BootNotification request
 */
enum ocpp_registration_status get_registration_status(void);

/**
 * @brief events that can be sent via xTaskNotify
 */
enum ocpp_task_event{
	eOCPP_TASK_CALL_ENQUEUED = 1<<0, ///< A new message has been prepared to be sendt to CS.
	eOCPP_TASK_CALL_TIMEOUT = 1<<1,
	eOCPP_TASK_FAILURE = 1<<2, ///< A fault has been detected in the ocpp task.
	eOCPP_TASK_REGISTRATION_STATUS_CHANGED = 1<<3 ///< BootNotification.conf has been recieved and status updated
};

#endif /* OCPP_TASK_H */
