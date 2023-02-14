#ifndef OCPP_CSL_H
#define OCPP_CSL_H

#include <stdbool.h>

/** @file
* @brief Contains helper function for ocpp Comma Seperated List (CSL)
*/

/**
 * @brief checks the csl to see if it contains the requested string
 * @param csl_container the csl to check
 * @param value the value to check for
 */
bool ocpp_csl_contains(const char * csl_container, const char * value);

#endif /*OCPP_CSL_H*/
