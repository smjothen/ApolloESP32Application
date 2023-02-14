#ifndef OCPP_DATE_TIME_H
#define OCPP_DATE_TIME_H

#include <time.h>

/** @file
* @brief Contains helper functions to parse and print OCPP dateTime
*/

/**
 * @brief parses a dateTime
 *
 * @param date_time dateTime to parse
 */
time_t ocpp_parse_date_time(char * date_time);

/**
 * @brief prints timestamp as valid dateTime
 *
 * @param timestamp timestamp to print
 * @param date_time_out buffer to print to
 * @param buffer_size length of buffer to print to
 */
int ocpp_print_date_time(time_t timestamp, char * date_time_out, size_t buffer_size);

#endif /*OCPP_DATE_TIME_H*/
