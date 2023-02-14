#ifndef OCPP_CI_STRING_TYPE_H
#define OCPP_CI_STRING_TYPE_H

#include <stdbool.h>

/** @file
* @brief Contains validation function for CiString<Length>Type
*/

/**
 * @brief Checks if string comply with the relevant CiString<Length>Type
 *
 * @param data the string to validate
 * @param length length of the maximum longest allowed CiString<Length>Type, should be 20, 25, 50, 255 or 500
 */
bool is_ci_string_type(const char * data, unsigned int length);

#endif /*OCPP_CI_STRING_TYPE_H*/
