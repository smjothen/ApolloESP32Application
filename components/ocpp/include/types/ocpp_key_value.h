#ifndef OCPP_KEY_VALUE_H
#define OCPP_KEY_VALUE_H

#include <stdbool.h>

#include <cJSON.h>

struct ocpp_key_value{
	const char * key;
	bool readonly;
	const char * value;
};

cJSON * create_key_value_json(struct ocpp_key_value key_value);

#endif /*OCPP_KEY_VALUE_H*/
