#ifndef _OCPP_H_
#define _OCPP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "types/ocpp_meter_value.h"

int ocpp_get_stack_watermark();
void save_interval_measurands(enum ocpp_reading_context_id context);
void handle_meter_value(enum ocpp_reading_context_id context, const char * csl, const char * stoptxn_csl,
			int * transaction_id, uint * connectors, size_t connector_count);
void ocpp_init();
// When graceful is enabled, it will attempt to stop ongoing transactions and send any queued transaction related messages.
void ocpp_end(bool graceful);
bool ocpp_is_running();
bool ocpp_task_exists();
void ocpp_restart(bool graceful);

#ifdef __cplusplus
}
#endif

#endif  /*_OCPP_H_*/
