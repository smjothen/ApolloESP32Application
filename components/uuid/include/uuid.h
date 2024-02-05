#ifndef __UUID_H__
#define __UUID_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>

#include "esp_random.h"

typedef struct {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_high_version;
	uint8_t clock_seq_high_reserved;
	uint8_t clock_seq_low;
	uint8_t node[6];
} uuid_t;

uuid_t uuid_generate(void);

bool uuid_from_string(uuid_t *uuid, const char *buf);
bool uuid_to_string(const uuid_t id, char *buf, size_t bufsize);

void uuid_from_bytes(uuid_t *id, uint8_t *bytes);
void uuid_to_bytes(const uuid_t id, uint8_t *bytes);

#endif
