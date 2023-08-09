#ifndef RFC3986_H
#define RFC3986_H

#include <stdbool.h>

void rfc3986_percent_encode(const char * s, char * encoded_buffer);
bool rfc3986_is_percent_encode_compliant(const char * s);
bool rfc3986_is_valid_uri(const char * s, const char ** uri_end);

#endif
