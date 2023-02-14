#ifndef OCPP_CREATE_METER_VALUE_H
#define OCPP_CREATE_METER_VALUE_H

#include <cJSON.h>
#include "ocpp_meter_value.h"

/** @file
* @brief Contains JSON helper for ocpp_meter_value
*/

/**
 * @brief converts a meter value to its JSON equivalent
 *
 * @param meter_value meter value to convert
 */
cJSON * create_meter_value_json(struct ocpp_meter_value meter_value);

#endif /*OCPP_CREATE_METER_VALUE_H*/
