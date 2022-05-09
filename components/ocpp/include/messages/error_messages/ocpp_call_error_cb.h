#ifndef OCPP_CALL_ERROR_CB_H
#define OCPP_CALL_ERROR_CB_H

#include "cJSON.h"

typedef void (*ocpp_error_callback) (const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details, void * cb_data);

#endif /*OCPP_CALL_ERROR_CB_H*/
