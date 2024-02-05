#include "esp_log.h"
#include "uuid.h"

// Adapted from Eldar's OCPP code ;)

uuid_t uuid_generate(void) {
	uuid_t id;

	esp_fill_random(&id, sizeof (uuid_t));

	id.clock_seq_high_reserved |= 0b10000000;
	id.clock_seq_high_reserved &= 0b10111111;

	id.time_high_version |= 0b0100000000000000;
	id.time_high_version &= 0b0100111111111111;

	return id;
}

bool uuid_from_string(uuid_t *uuid, const char *buf) {
	// Do simple verification of format
	for (size_t i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) { // Not a dash
			if (buf[i] != '-') {
				return false;
			}
		} else if (!isxdigit((int)buf[i])) { // Everything else must be hex
			return false;
		}
	}

	int len = 0;
	if (sscanf(buf, "%08" SCNx32 "-%04" SCNx16 "-%04" SCNx16 "-%02" SCNx8 "%02" SCNx8
				"-%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8  "%02" SCNx8  "%02" SCNx8 "%n",
				&uuid->time_low, &uuid->time_mid, &uuid->time_high_version, &uuid->clock_seq_high_reserved, &uuid->clock_seq_low,
				&uuid->node[0], &uuid->node[1], &uuid->node[2], &uuid->node[3], &uuid->node[4], &uuid->node[5], &len) != 11) {
		return false;
	}

	return len == 36;
}

bool uuid_to_string(const uuid_t id, char *buf, size_t bufsize) {
	snprintf(buf, bufsize, "%2.8" PRIx32 "-%4.4" PRIx16 "-%4.4" PRIx16 "-%2.2" PRIx8 "%2.2" PRIx8 "-%2.2" PRIx8 "%2.2" PRIx8 "%2.2" PRIx8 "%2.2" PRIx8 "%2.2" PRIx8 "%2.2" PRIx8,
		id.time_low, id.time_mid, id.time_high_version, id.clock_seq_high_reserved, id.clock_seq_low,
		id.node[0], id.node[1], id.node[2], id.node[3], id.node[4], id.node[5]);
	return true;
}
