#ifndef RFC3986_H
#define RFC3986_H

#include <stdbool.h>

void rfc3986_percent_encode(const unsigned char * s, char * encoded_buffer);
bool rfc3986_is_percent_encode_compliant(const unsigned char * s);
bool rfc3986_is_valid_uri(const unsigned char * s, const unsigned char ** uri_end);

#endif
