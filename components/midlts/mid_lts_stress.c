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

	if ((err = mid_session_init_internal(&ctx, maxpages, 0, fw, lr)) != LTS_OK) {
		ESP_LOGE(TAG, "Couldn't init MID session log! Error: %s", mid_session_err_to_string(err));
		return err;
	}
	return LTS_OK;
}

midlts_err_t midlts_stress_test(size_t maxpages, int n) {
	midlts_ctx_t ctx;
	midlts_err_t err;

	time_t time = MID_EPOCH;

	mid_session_version_fw_t fw = { 2, 0, 4, 201 };
	mid_session_version_lr_t lr = { 1, 2, 3 };

	ESP_LOGI(TAG, "Initializing with %zu max pages!", maxpages);

	if ((err = mid_session_init_internal(&ctx, maxpages, time, fw, lr)) != LTS_OK) {
		ESP_LOGE(TAG, "Couldn't init MID session log! Error: %s", mid_session_err_to_string(err));
		return err;
	}

	uint8_t buf[64] = {0};

	mid_session_auth_type_t types[] = {
		MID_SESSION_AUTH_TYPE_CLOUD,
		MID_SESSION_AUTH_TYPE_RFID,
		MID_SESSION_AUTH_TYPE_BLE,
		MID_SESSION_AUTH_TYPE_ISO15118,
	};

	uint32_t meter = 0;

	mid_session_auth_t last_auth = {0};
	mid_session_id_t last_id = {0};

#define MAX_METERS 256

	size_t meter_count = 0;
	static mid_session_meter_value_t last_meter[MAX_METERS];

	midlts_pos_t pos;

	if (MID_SESSION_IS_OPEN(&ctx)) {
		mid_session_record_t rec;
		if ((err = mid_session_add_close(&ctx, &pos, &rec, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
			ESP_LOGE(TAG, "Error appending initial session close : %s", mid_session_err_to_string(err));
			return err;
		}
		last_meter[meter_count++] = rec.meter_value;
	}

	uint32_t sess_length = 0;
	uint32_t auth_time = 0;
	uint32_t id_time = 0;
	uint32_t tariff_interval = 100;
	uint32_t nsess = 0;

	while (true) {

		if (MID_SESSION_IS_OPEN(&ctx) && sess_length > 0) {
			if (sess_length % tariff_interval == 0) {
				mid_session_record_t rec;
				// Tariff change
				if ((err = mid_session_add_tariff(&ctx, &pos, &rec, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session tariff : %s", mid_session_err_to_string(err));
					return err;
				}
				last_meter[meter_count++] = rec.meter_value;
			}

			if (sess_length == auth_time) {
				uint32_t size = 1 + esp_random() % 16;
				mid_session_record_t rec;
				if ((err = mid_session_add_auth(&ctx, &pos, &rec, time, types[esp_random() % 4], midlts_gen_rand(buf, size), size)) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session auth : %s", mid_session_err_to_string(err));
					return err;
				}
				last_auth = rec.auth;
			}

			if (sess_length == id_time) {
				uint32_t size = 16;
				mid_session_record_t rec;
				if ((err = mid_session_add_id(&ctx, &pos, &rec, time, midlts_gen_rand(buf, size))) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session id : %s", mid_session_err_to_string(err));
					return err;
				}
				last_id = rec.id;
			}

			sess_length--;
		} else if (MID_SESSION_IS_OPEN(&ctx)) {
			// Close
			mid_session_record_t rec;
			if ((err = mid_session_add_close(&ctx, &pos, &rec, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
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
			// ~2% chance of starting a session
			bool start = (esp_random() % 100) < 2;

			if (start) {
				mid_session_record_t rec;

				if ((err = mid_session_add_open(&ctx, &pos, &rec, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session open : %s", mid_session_err_to_string(err));
					return err;
				}
				last_meter[meter_count++] = rec.meter_value;

				if ((err = mid_session_add_id(&ctx, &pos, &rec, time, midlts_gen_rand(buf, 16))) != LTS_OK) {
					ESP_LOGE(TAG, "Session id : %s", mid_session_err_to_string(err));
					return err;
				}
				last_id = rec.id;

				sess_length = tariff_interval * (esp_random() % 11); // 0 - 1000 ticks
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
		if (time % tariff_interval == 0) {
			mid_session_record_t rec;
			if ((err = mid_session_add_tariff(&ctx, &pos, &rec, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter)) != LTS_OK) {
				ESP_LOGE(TAG, "Tariff : %s", mid_session_err_to_string(err));
				return err;
			}
			if (MID_SESSION_IS_OPEN(&ctx)) {
				// Add to active session if open!
				last_meter[meter_count++] = rec.meter_value;
			}
		}

		time++;
	}
}

#ifdef HOST

int main(int argc, char **argv) {

	midlts_err_t err;

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

