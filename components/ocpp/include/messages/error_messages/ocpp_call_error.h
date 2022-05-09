#ifndef OCPP_CALL_ERROR_H
#define OCPP_CALL_ERROR_H

#include "cJSON.h"

cJSON * ocpp_create_call_error(const char * unique_id, const char * error_code, const char * error_description, cJSON * error_details);

#endif /*OCPP_CALL_ERROR_H*/
