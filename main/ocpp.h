#ifndef _OCPP_H_
#define _OCPP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "types/ocpp_meter_value.h"

int ocpp_get_stack_watermark();
void save_interval_measurands(const char * context);
void handle_meter_value(const char * context, const char * csl, int * transaction_id, uint * connectors, size_t connector_count);
void ocpp_init();
void ocpp_end();
bool ocpp_is_running();
bool ocpp_task_exists();
void ocpp_restart();

#ifdef __cplusplus
}
#endif

#endif  /*_OCPP_H_*/
