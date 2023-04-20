#include <string.h>
#include "types/ocpp_ci_string_type.h"

bool is_ci_string_type(const char * data, unsigned int length){
	switch(length){
	case 20:
	case 25:
	case 50:
	case 40: //not a true ci_string but used for string in security whitepaper
	case 255:
	case 500:
		break;
	default:
		return false;
	}

	return (strnlen(data, length + 1) <= length);
}
