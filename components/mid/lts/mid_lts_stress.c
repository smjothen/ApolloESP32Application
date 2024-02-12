#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>

#include "mid_session.h"

#include "mid_lts.h"
#include "mid_lts_priv.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDSTRESS       ";

static uint8_t *midlts_gen_rand(uint8_t *out, size_t size) {
	for (size_t i = 0; i < size; i++) {
		out[i] = esp_random();
	}
	return out;
}

midlts_err_t midlts_replay(size_t maxpages) {
	midlts_ctx_t ctx;
	midlts_err_t err;

	mid_session_version_fw_t fw = { 2, 0, 4, 201 };
	mid_session_version_lr_t lr = { 1, 2, 3 };

	if ((err = mid_session_init_internal(&ctx, maxpages, fw, lr)) != LTS_OK) {
		ESP_LOGE(TAG, "Couldn't init MID session log! Error: %s", mid_session_err_to_string(err));
		return err;
	}
	return LTS_OK;
}

midlts_err_t midlts_stress_test(size_t maxpages, int n) {
	midlts_ctx_t ctx;
	midlts_err_t err;


	mid_session_version_fw_t fw = { 2, 0, 4, 201 };
	mid_session_version_lr_t lr = { 1, 2, 3 };

	ESP_LOGI(TAG, "Initializing with %zu max pages!", maxpages);

	if ((err = mid_session_init_internal(&ctx, maxpages, fw, lr)) != LTS_OK) {
		ESP_LOGE(TAG, "Couldn't init MID session log! Error: %s", mid_session_err_to_string(err));
		return err;
	}

	// Use latest meter value time + 1 to 'continue' the sequence
	uint64_t time = ctx.msg_latest.time + 1;
	uint32_t meter = ctx.msg_latest.meter + 1;

	uint8_t buf[64] = {0};

	mid_session_auth_source_t sources[] = {
		MID_SESSION_AUTH_SOURCE_UNKNOWN,
		MID_SESSION_AUTH_SOURCE_RFID,
		MID_SESSION_AUTH_SOURCE_BLE,
		MID_SESSION_AUTH_SOURCE_ISO15118,
		MID_SESSION_AUTH_SOURCE_CLOUD,
	};
	size_t nsources = sizeof (sources) / sizeof (sources[0]);

	mid_session_auth_type_t types[] = {
		MID_SESSION_AUTH_TYPE_RFID,
		MID_SESSION_AUTH_TYPE_UUID,
		MID_SESSION_AUTH_TYPE_EMAID,
		MID_SESSION_AUTH_TYPE_EVCCID,
		MID_SESSION_AUTH_TYPE_STRING,
		MID_SESSION_AUTH_TYPE_UNKNOWN,

	};
	size_t ntypes = sizeof (types) / sizeof (types[0]);

	mid_session_auth_t last_auth = {0};
	mid_session_id_t last_id = {0};

#define MAX_METERS 256
#define MAX_SESS_LENGTH (1000 * 12 * 60 * 60 + 1)
#define TARIFF_INTERVAL (1000 * 60 * 60) // 1 hour

	size_t meter_count = 0;
	static mid_session_meter_value_t last_meter[MAX_METERS];

	midlts_pos_t pos;

	if (MID_SESSION_IS_OPEN(&ctx)) {
		mid_session_record_t rec;
		if ((err = mid_session_add_close(&ctx, &pos, &rec, MID_TIME_TO_TS(time), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
			ESP_LOGE(TAG, "Error appending initial session close : %s", mid_session_err_to_string(err));
			return err;
		}
	}

	uint32_t sess_length = 0;
	uint32_t auth_time = 0;
	uint32_t id_time = 0;
	uint32_t nsess = 0;

	uint32_t last_tariff = 0;

	while (true) {
		if (MID_SESSION_IS_OPEN(&ctx) && sess_length > 0) {
			if (time % TARIFF_INTERVAL == 0) {
				mid_session_record_t rec;
				// Tariff change
				last_tariff = meter;
				if ((err = mid_session_add_tariff(&ctx, &pos, &rec, MID_TIME_TO_TS(time), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session tariff : %s", mid_session_err_to_string(err));
					return err;
				}
				ESP_LOGI(TAG, "%d:%d", ctx.active_session.pos.id, ctx.active_session.pos.offset);
				last_meter[meter_count++] = rec.meter_value;
			}

			if (sess_length == auth_time) {
				uint32_t size = 1 + esp_random() % 16;
				mid_session_record_t rec;
				if ((err = mid_session_add_auth(&ctx, &pos, &rec, MID_TIME_TO_TS(time), sources[esp_random() % nsources], types[esp_random() % ntypes], midlts_gen_rand(buf, size), size)) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session auth : %s", mid_session_err_to_string(err));
					return err;
				}
				last_auth = rec.auth;
			}

			if (sess_length == id_time) {
				uint32_t size = 16;
				mid_session_record_t rec;
				if ((err = mid_session_add_id(&ctx, &pos, &rec, MID_TIME_TO_TS(time), midlts_gen_rand(buf, size))) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session id : %s", mid_session_err_to_string(err));
					return err;
				}
				last_id = rec.id;
			}

			sess_length--;
		} else if (MID_SESSION_IS_OPEN(&ctx)) {
			// Close
			mid_session_record_t rec;
			if ((err = mid_session_add_close(&ctx, &pos, &rec, MID_TIME_TO_TS(time), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
				ESP_LOGE(TAG, "Session close : %s", mid_session_err_to_string(err));
				return err;
			}
			last_meter[meter_count++] = rec.meter_value;

			// Verify the active session is as expected!

			assert(meter_count == ctx.active_session.count);
			assert(memcmp(&last_id, &ctx.active_session.id, sizeof (last_id)) == 0);
			assert(memcmp(&last_auth, &ctx.active_session.auth, sizeof (last_auth)) == 0);
			assert(memcmp(last_meter, ctx.active_session.events, sizeof (last_meter[0]) * meter_count) == 0);

			// Clear
			memset(&last_id, 0, sizeof (last_id));
			memset(&last_auth, 0, sizeof (last_auth));
			meter_count = 0;

			nsess++;
			if (nsess == n) {
				return 0;
			}

		} else {
			// ~0.02% chance of starting a session
			bool start = (esp_random() % 10000) < 2;

			if (start) {
				mid_session_record_t rec;

				if ((err = mid_session_add_open(&ctx, &pos, &rec, MID_TIME_TO_TS(time), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session open : %s", mid_session_err_to_string(err));
					return err;
				}
				last_meter[meter_count++] = rec.meter_value;

				if ((err = mid_session_add_id(&ctx, &pos, &rec, MID_TIME_TO_TS(time), midlts_gen_rand(buf, 16))) != LTS_OK) {
					ESP_LOGE(TAG, "Session id : %s", mid_session_err_to_string(err));
					return err;
				}
				last_id = rec.id;

				sess_length = esp_random() % (1000 * 12 * 60 * 60 + 1); // 0 - 12 hours in ms
				if (sess_length) {
					auth_time = esp_random() % sess_length;
					id_time = esp_random() % sess_length;
				} else {
					auth_time = 0;
					id_time = 0;
				}
			}
		}

		// TODO: Only if meter changed since last meter value?
		if (MID_SESSION_IS_CLOSED(&ctx) && (time % TARIFF_INTERVAL == 0) && last_tariff != meter) {
			mid_session_record_t rec;
			if ((err = mid_session_add_tariff(&ctx, &pos, &rec, MID_TIME_TO_TS(time), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter)) != LTS_OK) {
				ESP_LOGE(TAG, "Tariff : %s", mid_session_err_to_string(err));
				last_tariff = meter;
				return err;
			}
		}

		time++;
	}
}

#ifdef HOST

int main(int argc, char **argv) {

	midlts_err_t err = LTS_OK;

	size_t maxpages = 4;
	char c;

	while ((c = getopt (argc, argv, "p:rx:")) != -1) {
		switch (c) {
			case 'p':
				maxpages = atoi(optarg);
				break;
			case 'r':
				err = midlts_replay(maxpages);
				break;
			case 'x':
				err = midlts_stress_test(maxpages, atoi(optarg));
				break;
			case '?':
			default:
				break;
		}
	}

	if (err != LTS_OK) {
		ESP_LOGE(TAG, "Error: %s", mid_session_err_to_string(err));
		return -1;
	}

	return 0;
}

#endif

