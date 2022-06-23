#ifndef OCPP_DATE_TIME_H
#define OCPP_DATE_TIME_H

#include <time.h>

time_t ocpp_parse_date_time(const char * date_time);
int ocpp_print_date_time(time_t timestamp, char * date_time_out, size_t buffer_size);

#endif /*OCPP_DATE_TIME_H*/
