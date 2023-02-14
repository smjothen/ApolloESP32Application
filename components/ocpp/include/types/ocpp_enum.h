#ifndef OCPP_ENUM_H
#define OCPP_ENUM_H
#include <stdbool.h>

/** @file
* @brief Contains the helper to convert JSON string enums to C defines
*/

/**
 * @brief validates that a value is within an enum
 *
 * @param value the value to check for
 * @param case_sensitive if true then value must also match with expected case
 * @param option_count length of the following va_list with type const char * matching the elemnts in the JSON string enum
 */
int ocpp_validate_enum(const char * value, bool case_sensitive, unsigned int option_count, ...);

#endif /*OCPP_ENUM_H*/
