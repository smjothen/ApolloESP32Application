#ifndef _OCPP_H_
#define _OCPP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "types/ocpp_meter_value.h"

int ocpp_get_stack_watermark();
int ocpp_populate_meter_values(uint connector_id, const char * context,
			const char * measurand_csl, struct ocpp_meter_value * meter_value_out);
void ocpp_init();

#ifdef __cplusplus
}
#endif

#endif  /*_OCPP_H_*/
