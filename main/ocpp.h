#ifndef _OCPP_H_
#define _OCPP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "types/ocpp_meter_value.h"
#include "cJSON.h"

cJSON * ocpp_get_diagnostics();
int ocpp_get_stack_watermark();
void ocpp_task_clear_connection_delay();
void init_interval_measurands(enum ocpp_reading_context_id context);
void save_interval_measurands(enum ocpp_reading_context_id context);
void handle_meter_value(enum ocpp_reading_context_id context, const char * csl, const char * stoptxn_csl,
			int * transaction_id, bool valid_id, uint * connectors, size_t connector_count, bool is_trigger);
void ocpp_init();
// When graceful is enabled, it will attempt to stop ongoing transactions and send any queued transaction related messages.
// Note that all ocpp_end* functions prepares the ocpp thread and notifies it will therefore not exit imediatly.
void ocpp_end(bool graceful);
bool ocpp_is_running();
bool ocpp_is_exiting();
bool ocpp_task_exists();
void ocpp_end_and_reboot(bool graceful);
void ocpp_end_and_reconnect(bool graceful);

#ifdef __cplusplus
}
#endif

#endif  /*_OCPP_H_*/
