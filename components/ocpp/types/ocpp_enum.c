#include <stdarg.h>
#include <string.h>

#include "types/ocpp_enum.h"

int ocpp_validate_enum(const char * value, bool case_sensitive, unsigned int option_count, ...){
	va_list argument_ptr;

	va_start(argument_ptr, option_count);

	for(int i = 0; i < option_count; i++){
		const char * enum_value = va_arg(argument_ptr, const char *);

		int comparison_result;
		if(case_sensitive){
			comparison_result = strcmp(value, enum_value);
		}else{
			comparison_result = strcasecmp(value, enum_value);
		}

		if(comparison_result == 0){
			va_end(argument_ptr);
			return 0;
		}
	}
	va_end(argument_ptr);
	return -1;
}
