#ifndef OCPPJ_VALIDATION_H
#define OCPPJ_VALIDATION_H

#include <stdbool.h>

#include "cJSON.h"

#include "ocppj_message_structure.h"

/** @file
 * @brief Contains functions to check if a payload contains expected fields.
 */

/**
 * @brief Gets a decimal value
 *
 * @param container JSON object in the payload that should contain the value
 * @param field_name The field the JSON should contain
 * @param required Should be true if OCPP specifies the field as required
 * @param value_out The value of the field if pressent and valid
 * @param error_description_out string to write error message to if field is invalid
 * @param error_description_length buffer size of error description
 */
enum ocppj_err_t ocppj_get_decimal_field(cJSON * container, const char * field_name, bool required, double * value_out,
				char * error_description_out, size_t error_description_length);

/**
 * @brief Gets a int value
 *
 * @param container JSON object in the payload that should contain the value
 * @param field_name The field the JSON should contain
 * @param required Should be true if OCPP specifies the field as required
 * @param value_out The value of the field if pressent and valid
 * @param error_description_out string to write error message to if field is invalid
 * @param error_description_length buffer size of error description
 */
enum ocppj_err_t ocppj_get_int_field(cJSON * container, const char * field_name, bool required, int * value_out,
				char * error_description_out, size_t error_description_length);

/**
 * @brief Gets a string value
 *
 * @param container JSON object in the payload that should contain the value
 * @param field_name The field the JSON should contain
 * @param required Should be true if OCPP specifies the field as required
 * @param value_out The value of the field if pressent and valid
 * @param error_description_out string to write error message to if field is invalid
 * @param error_description_length buffer size of error description
 */
enum ocppj_err_t ocppj_get_string_field(cJSON * container, const char * field_name, bool required, char ** value_out,
				char * error_description_out, size_t error_description_length);

#endif /*OCPPJ_VALIDATION_H*/
