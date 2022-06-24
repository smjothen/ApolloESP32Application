#ifndef OCPP_CREATE_METER_VALUE_H
#define OCPP_CREATE_METER_VALUE_H

#include <cJSON.h>
#include "ocpp_meter_value.h"

cJSON * create_meter_value_json(struct ocpp_meter_value meter_value);

#endif /*OCPP_CREATE_METER_VALUE_H*/
