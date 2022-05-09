#ifndef OCPP_CALL_RESULT_CB_H
#define OCPP_CALL_RESULT_CB_H

#include "cJSON.h"

typedef void (*ocpp_result_callback) (const char * unique_id, cJSON * payload, void * cb_data);

#endif /*OCPP_CALL_RESULT_CB_H*/
