#ifndef OCPP_RESERVATION_H_
#define OCPP_RESERVATION_H_

#include <time.h>
#include <stdbool.h>

#include <esp_err.h>
#include "cJSON.h"
#include "types/ocpp_id_token.h"

/**
 * @brief Info from a ReserveNow.req
 */
struct ocpp_reservation_info {
	int connector_id; ///< This contains the id of the connector to be reserved. A value of 0 means that the reservation is not for a specific connector.
	time_t expiry_date; ///< This contains the date and time when the reservation ends.
	ocpp_id_token id_tag; ///< The identifier for which the Charge Point has to reserve a connector.
	ocpp_id_token parent_id_tag; ///< The parent idTag.
	int reservation_id; ///< Unique id for this reservation.
};

/**
 * @brief Caches and stores the given ocpp_reservation on file
 *
 * @para info The new reservation as described by ReserveNow.req
 */
esp_err_t ocpp_reservation_set_info(struct ocpp_reservation_info * info);

/**
 * @brief Gets the cached reservation info or loads it from file if no reservation was found.
 *
 * @return The reservation or NULL if no reservation is active (expired or deleted)
 */
struct ocpp_reservation_info * ocpp_reservation_get_info();

/**
 * @brief Deletes any reservation that currently exists
 */
esp_err_t ocpp_reservation_clear_info();

/**
 * @brief get a json object containing information about the ocpp reservation status
 */
cJSON * ocpp_reservation_get_diagnostics();

/**
 * @brief initializes the reservation component (sets relevant semaphore)
 */
esp_err_t ocpp_reservation_init();

/**
 * @brief Frees memory required by reservation component
 */
esp_err_t ocpp_reservation_deinit();
#endif // OCPP_RESERVATION_H_
