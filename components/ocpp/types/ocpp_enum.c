#include <stdarg.h>
#include <string.h>

#include "types/ocpp_enum.h"

int ocpp_validate_enum(const char * value, unsigned int option_count, ...){
	va_list argument_ptr;

	va_start(argument_ptr, option_count);

	for(int i = 0; i < option_count; i++){
		const char * enum_value = va_arg(argument_ptr, const char *);
		if(strcmp(value, enum_value) == 0){
			va_end(argument_ptr);
			return 0;
		}
	}
	va_end(argument_ptr);
	return -1;
}
