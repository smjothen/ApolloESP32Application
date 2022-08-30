#ifndef OCPP_ENUM_H
#define OCPP_ENUM_H
#include <stdbool.h>

int ocpp_validate_enum(const char * value, bool case_sensitive, unsigned int option_count, ...);

#endif /*OCPP_ENUM_H*/
