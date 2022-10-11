#ifndef OCPPJ_VALIDATION_H
#define OCPPJ_VALIDATION_H

#include <stdbool.h>

#include "cJSON.h"

#include "ocppj_message_structure.h"

enum ocppj_err_t ocppj_get_decimal_field(cJSON * container, const char * field_name, bool required, double * value_out,
				char * error_description_out, size_t error_description_length);

enum ocppj_err_t ocppj_get_int_field(cJSON * container, const char * field_name, bool required, int * value_out,
				char * error_description_out, size_t error_description_length);

enum ocppj_err_t ocppj_get_string_field(cJSON * container, const char * field_name, bool required, char ** value_out,
				char * error_description_out, size_t error_description_length);

#endif /*OCPPJ_VALIDATION_H*/
