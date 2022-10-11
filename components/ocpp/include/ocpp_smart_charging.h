#ifndef OCPP_SMART_CHARGING_H
#define OCPP_SMART_CHARGING_H

#include "types/ocpp_charging_profile.h"

/**
 * Initializes smart charging, setting configuration values, mounting file system and attaching ocpp callbacks and
 * starting the thread that handles and informs about new charging limits due to change of profile/schedule/schedule_period.
 *
 * The thread will mostly stay dormant untill woken by ocpp set/clear/ charging profile request, get schedule request or if
 * notified about new transaction or transactionid. NOTE: it might be possible to
 * implement using timer istead of thread. Currently thread is being used due to "direct to task notifications"
 */
esp_err_t ocpp_smart_charging_init(size_t connector_count, int max_stack_level,
				const char * allowed_charging_rate_unit, int max_periods, int max_charging_profiles);

void ocpp_set_active_transaction_id(int * transaction_id);
void ocpp_set_transaction_is_active(bool active);
void ocpp_set_on_new_period_cb(void (* on_new_period)(float min_charging_limit, float max_charging_limit, uint8_t number_phases));
#endif /* OCPP_SMART_CHARGING_H */
