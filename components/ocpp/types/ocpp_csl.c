#include <string.h>

#include "types/ocpp_csl.h"

bool ocpp_csl_contains(const char * csl_container, const char * value){
	size_t value_length = strlen(value);

	if(value_length == 0)
		return false;

	char * value_in_container = strstr(csl_container, value);
	while(value_in_container != NULL){
		if((value_in_container == csl_container || *(value_in_container-1) == ',') // Is first csl item or preceded by ','
			&& (*(value_in_container + strlen(value)) == '\0' || *(value_in_container + strlen(value)) == ',')){ // is last csl item or followed by ','
			return true;
		}

		value_in_container = strstr(value_in_container, value);
	}

	return false;
}
