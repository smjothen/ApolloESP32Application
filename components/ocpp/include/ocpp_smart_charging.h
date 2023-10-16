#ifndef OCPP_SMART_CHARGING_H
#define OCPP_SMART_CHARGING_H

#include "types/ocpp_charging_profile.h"

/** @file
 * @brief Handles the Smart charging profile of OCPP.
 *
 * It handles ClearChargingProfile.req, GetCompositeSchedule.req, SetChargingProfile.req and allows integration with
 * charging session handler to notify it of wanted change of charging limits and used nr of phases.
 */


/**
 * @brief returns free bytes on ocpp smart
 */
int ocpp_smart_get_stack_watermark();


/**
 * @brief Initializes smart charging.
 *
 * setting configuration values, mounting file system, attaching ocpp callbacks and
 * starts the thread that handles and informs about new charging limits due to change of profile/schedule/period.
 *
 * The thread will mostly stay dormant untill woken by ocpp set/clear/ charging profile request, get schedule request or if
 * notified about new transaction or transactionid.
 */
esp_err_t ocpp_smart_charging_init();

/**
 * @brief exits and frees memory allocated by the smart charging thread
 */
void ocpp_smart_charging_deinit();

/**
 * @brief Used to inform smart charging of id in case new charging profile is transaction id specific.
 *
 * @param transaction_id id of new transaction or NULL if no transaction
 */
void ocpp_set_active_transaction_id(int * transaction_id);


/**
 * @brief gives a new ChargingProfile to the smart charging handler. This call transfers responsibility of freeing to smart charging component.
 *
 * @param profile the new ChargingProfile
 */
esp_err_t update_charging_profile(struct ocpp_charging_profile * profile);

/**
 * @brief Informs the smart charging that transaction has begun or has ended
 *
 * Expects the ocpp_set_active_transaction_id to have been called previously
 *
 * @param active Should be true if transaction is active
 * @param start_time when the transaction started
 */
void ocpp_set_transaction_is_active(bool active, time_t start_time);

/**
 * @brief sets a callback when minimum/maximum charge limit or number of phases to be used changes.
 *
 * @brief on_new_period callback function to set
 */
void ocpp_set_on_new_period_cb(void (* on_new_period)(float min_charging_limit, float max_charging_limit, uint8_t number_phases));

/**
 * @brief get a json object containing information about the ocpp smart charging state.
 */
cJSON * ocpp_smart_get_diagnostics();

#endif /* OCPP_SMART_CHARGING_H */
