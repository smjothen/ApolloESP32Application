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
		MID_SESSION_AUTH_TYPE_NEXTGEN,
	};

	uint32_t meter = 0;

	midlts_pos_t pos;

	if (MID_SESSION_IS_OPEN(&ctx)) {
		if ((err = mid_session_add_close(&ctx, &pos, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
			ESP_LOGE(TAG, "Error appending initial session close : %s", mid_session_err_to_string(err));
			return err;
		}
	}

	uint32_t sess_length = 0;
	uint32_t auth_time = 0;
	uint32_t id_time = 0;
	uint32_t tariff_interval = 100;
	uint32_t nsess = 0;

	while (true) {

		if (MID_SESSION_IS_OPEN(&ctx) && sess_length > 0) {
			if (sess_length % tariff_interval == 0) {
				// Tariff change
				if ((err = mid_session_add_tariff(&ctx, &pos, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session tariff : %s", mid_session_err_to_string(err));
					return err;
				}
			}

			if (sess_length == auth_time) {
				uint32_t size = 1 + esp_random() % 16;
				if ((err = mid_session_add_auth(&ctx, &pos, time, types[esp_random() % 5], midlts_gen_rand(buf, size), size)) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session auth : %s", mid_session_err_to_string(err));
					return err;
				}
			}

			if (sess_length == id_time) {
				uint32_t size = 16;
				if ((err = mid_session_add_id(&ctx, &pos, time, midlts_gen_rand(buf, size))) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session id : %s", mid_session_err_to_string(err));
				}
			}

			sess_length--;
		} else if (MID_SESSION_IS_OPEN(&ctx)) {
			// Close

			if ((err = mid_session_add_close(&ctx, &pos, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
				ESP_LOGE(TAG, "Session close : %s", mid_session_err_to_string(err));
				return err;
			}

			nsess++;
			if (nsess == n) {
				return 0;
			}

		} else {
			// ~2% chance of starting a session
			bool start = (esp_random() % 100) < 2;

			if (start) {
				if ((err = mid_session_add_open(&ctx, &pos, time, midlts_gen_rand(buf, 16), MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter++)) != LTS_OK) {
					ESP_LOGE(TAG, "Session open : %s", mid_session_err_to_string(err));
					return err;
				}

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
			if ((err = mid_session_add_tariff(&ctx, &pos, time, MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED, meter)) != LTS_OK) {
				ESP_LOGE(TAG, "Tariff : %s", mid_session_err_to_string(err));
				return err;
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

