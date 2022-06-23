#include <stdio.h>

#include "types/ocpp_date_time.h"
#include "types/ocpp_enum.h"

time_t ocpp_parse_date_time(const char * date_time){
	// the date time in a json schema is compliant with RFC 5.6 https://json-schema.org/understanding-json-schema/reference/string.html
	// e.g. "1985-04-12T23:20:50.52Z", "1990-12-31T15:59:60-08:00"
	// parseing based on https://stackoverflow.com/questions/26895428/how-do-i-parse-an-iso-8601-date-with-optional-milliseconds-to-a-struct-tm-in-c

	int y,M,d,h,m;
	float s;
	int tzh = 0, tzm = 0;

	int paresed_arguments = sscanf(date_time, "%d-%d-%dT%d:%d:%f%d:%dZ", &y, &M, &d, &h, &m, &s, &tzh, &tzm);

	if (6 < paresed_arguments) {
		if (tzh < 0) {
			tzm = -tzm;    // Fix the sign on minutes.
		}
	}

	struct tm time;
	time.tm_year = y - 1900; // Year since 1900
	time.tm_mon = M - 1;     // 0-11
	time.tm_mday = d;        // 1-31
	time.tm_hour = h;        // 0-23
	time.tm_min = m;         // 0-59
	time.tm_sec = (int)s;    // 0-61 (0-60 in C++11)

	//TODO: return tz and maybe handle sub-sec resolution
	//TODO: validate and Sanity check the time

	return mktime(&time);
}

int ocpp_print_date_time(time_t timestamp, char * date_time_out, size_t buffer_size){
	return strftime(date_time_out, buffer_size, "%FT%T%Z", localtime(&timestamp));
}
