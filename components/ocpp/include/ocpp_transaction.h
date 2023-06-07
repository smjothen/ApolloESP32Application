#ifndef OCPP_TRANSACTION_H
#define OCPP_TRANSACTION_H

#include "esp_websocket_client.h"

#include "ocpp_task.h"
#include "ocpp_call_with_cb.h"

#include "types/ocpp_reason.h"
#include "types/ocpp_meter_value.h"
#include "types/ocpp_id_token.h"

/** @file
 * @brief Handles transaction state in case of going offline or unexpected reboot.
 */

/**
 * @brief callback structure for StartTransaction.conf and StopTransaction.conf
 */
struct ocpp_transaction_start_stop_cb_data{
	int transaction_entry; ///< Entry on file used for this transaction
	ocpp_id_token id_tag; ///< idToken used to start or stop transaction if present
};

/**
 * @brief Sets callbacks for transaction related messages.
 *
 * @note although the callback signature is the same as for other OCPP messages, the callback data can not
 * be known before a response has been recieved as the messages may have been written to file and sent after
 * a reboot has occured. The callback data will be populated with statically allocated data and should be
 * treated as a \ref ocpp_transaction_start_stop_cb_data struct for start and stop messages and a const char array
 * for meter values stored on file.
 *
 * @param start_transaction_result_cb callback function on StartTransaction.conf
 * @param start_transaction_error_cb callback function on failed StartTransaction.req
 * @param stop_transaction_result_cb callback function on StopTransaction.conf
 * @param stop_transaction_error_cb callback function on failed StopTransaction.req
 * @param meter_transaction_result_cb callback function on MeterValues.conf
 * @param meter_transaction_error_cb callback function on failed MeterValues.req
 */
void ocpp_transaction_set_callbacks(
	ocpp_result_callback start_transaction_result_cb, ocpp_error_callback start_transaction_error_cb,
	ocpp_result_callback stop_transaction_result_cb, ocpp_error_callback stop_transaction_error_cb,
	ocpp_result_callback meter_transaction_result_cb, ocpp_error_callback meter_transaction_error_cb);

/**
 * @brief Gets the timestamp of the oldest message stored on file or in transaction queue.
 */
time_t ocpp_transaction_get_oldest_timestamp();

/**
 * @brief Gets the number of unique transaction files. A new transaction file is created as a result of enqueue_start. And deleted on StopTransaction.conf or error.
 *
 */
int ocpp_transaction_count();

/**
 * @brief Gets the number of transaction messages that are waiting to get a .conf or error result.
 */
size_t ocpp_transaction_message_count();

/**
 * @brief Gets the entry nr of the active entry or -1 if no active entry can be found. An active entry is one that has a StartTransaction.req but no StopTransaction.req.
 *
 * @param connector_id connector number used in the StartTransaction.req.
 */
int ocpp_transaction_find_active_entry(int connector_id);

/**
 * @brief Gets the next transaction message in chronological order.
 *
 * @param call output parameter containing the next message on success.
 *
 * @return pdTRUE if a message was found. pdFalse if no message could be created or found.
 */
BaseType_t ocpp_transaction_get_next_message(struct ocpp_active_call * call);

/**
 * @brief Adds a transaction message to the queue without storing on file.
 *
 * @details May be used when storing messages on file may cause more issues than benefits. If used with invalid id, then the id will never be updated.
 * Should only be used for transaction related MeterValues.req as StartTransaction.req and StopTransaction.req are used to controll state of transaction files.
 *
 * @param message the message to put on the transaction queue
 * @param wait the maximum ticks to wait for message to be put on the FreeRTOS queue.
 */
BaseType_t ocpp_transaction_queue_send(struct ocpp_call_with_cb ** message, TickType_t wait);

/**
 * @brief stores a StartTransaction.req on file and prepares transaction state for consistency.
 *
 * @param connector_id nr of the connector where the transaction started.
 * @param id_tag id token used to start the transaction.
 * @param meter_start emeter value at the start of the transaction.
 * @param reservation_id id of related reservation or null if no reservation is relevant.
 * @param timestamp when the tranaction started.
 * @param entry_out output parameter containing the transaction file entry if file was created successfully.
 */
int ocpp_transaction_enqueue_start(int connector_id, const char * id_tag, int meter_start, int * reservation_id, time_t timestamp, int * entry_out);

/**
 * @brief stores a transaction related MeterValue.req on file if deemed necessary, else it will add it to the transaction queue.
 *
 * @param connector_id connector nr where value was sampled.
 * @param transaction_id id of the transaction it relates to. Should be NULL if no valid transaction id is known.
 * @param meter_values list of sampled values to send.
 */
int ocpp_transaction_enqueue_meter_value(unsigned int connector_id, const int * transaction_id, struct ocpp_meter_value_list * meter_values);

/**
 * @brief stores a StopTransaction.req on file and sets the transaction as no longer active/ongoing.
 *
 * @param id_tag id token used to stop the transaction or NULL if stopped for any other reason.
 * @param meter_stop emeter value at the end of transaction.
 * @param timestamp when the transaction stopped.
 * @param reason why the transaction was stopped.
 * @param transaction_data MeterValue as part of the stop tranaction message.
 */
int ocpp_transaction_enqueue_stop(const char * id_tag, int meter_stop, time_t timestamp, enum ocpp_reason_id reason, struct ocpp_meter_value_list * transaction_data);

/**
 * @brief sets the transaction id of a transaction entry, expected to be called with id given in a StartTransaction.conf
 *
 * @param entry the transaction file number that should be updated.
 * @param transaction_id the new id the transaction should have.
 */
esp_err_t ocpp_transaction_set_real_id(int entry, int transaction_id);

/**
 * @brief loades the cb data for the currently active transaction call if relevant data exits.
 *
 * @param cb_data_out output buffer to write cb data to.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_PARAM if cb_data_out is NULL, ESP_ERR_INVALID STATE if
 * no transaction was loaded from file, ESP_ERR_NOT_FOUND if No relevant data found.
 */
esp_err_t ocpp_transaction_load_cb_data(struct ocpp_transaction_start_stop_cb_data * cb_data_out);

/**
 * @brief Updates the transaction state to indicate that a .conf or error response has been received.
 */
esp_err_t ocpp_transaction_confirm_last();

/**
 * @brief Get the information needed to continue an active transaction that was ongoing during last boot
 * @param transaction_start_out output parameter for the timestamp of the StartTransaction.req.
 * @param stored_token_out output parameter used for the token id used to start the transaction.
 * @param transaction_id_out output parameter for the transaction id given in the StartTransaction.conf if received.
 * @param transaction_id_valid_out output parameter that is true if transaction id is the result of StartTransaction.conf. If false then id is entry nr on file.
 */
esp_err_t ocpp_transaction_load_into_session(time_t * transaction_start_out, ocpp_id_token stored_token_out, int * transaction_id_out, bool * transaction_id_valid_out);


/**
 * @brief removes all transaction related files
 */
int ocpp_transaction_clear_all();

/**
 * @brief attempts to call error callback for all transaction messages and removes all transaction files
 *
 * @param error_description error description given to the error callback.
 */
void ocpp_transaction_fail_all(const char * error_description);

/**
 * @brief sets a task handle that will be notified of relevant events
 *
 * @param task handle for task to noify
 * @param offset may be used to leftshift the notified value to allow multiple notifiers.
 */
void ocpp_transaction_configure_task_notification(TaskHandle_t task, uint offset);

/**
 * @brief initiates the transaction component to ensure filesystem can be used and transaction queue exists.
 */
int ocpp_transaction_init();

/**
 * @brief checks if component has been initiated successfully
 */
bool ocpp_transaction_is_ready();

/**
 * @brief frees the resources allocated for transaction storage.
 */
void ocpp_transaction_deinit();

#endif /*OCPP_TRANSACTION_H*/
