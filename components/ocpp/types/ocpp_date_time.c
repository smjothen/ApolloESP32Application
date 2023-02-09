#include <stdio.h>

#include "types/ocpp_date_time.h"
#include "types/ocpp_enum.h"
#include "utz.h"

//TODO: consider adding support for full ISO standard (non utc input, duration, default fields).

time_t ocpp_parse_date_time(char * date_time){

	udatetime_t dt;
	if(utz_datetime_parse_iso(date_time, &dt) != 0)
		return (time_t)-1;

	return utz_datetime_to_unix(&dt);
}

int ocpp_print_date_time(time_t timestamp, char * date_time_out, size_t buffer_size){
	udatetime_t dt;
	utz_unix_to_datetime(timestamp, &dt);

	return utz_datetime_format_iso_utc(date_time_out, buffer_size, &dt);
}
