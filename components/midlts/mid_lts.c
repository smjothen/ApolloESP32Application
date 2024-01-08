#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "mid_session.h"
#include "mid_lts.h"

// Limit age to <= 31 days (1 month), should be enough time to store the max amount of
// session files below!
#define MIDLTS_MAX_AGE 31
// TODO: Increase this limit with littlefs? Very few chargers do have > 100 sessions in a month
#define MIDLTS_MAX_FILES 100
#define MIDLTS_SIZE(len) (sizeof (midsess_t) + (len) * sizeof (midsess_meter_val_t))

#define MIDLTS_SCN "%" SCNx32 ".ms%n"
#define MIDLTS_PRI "%" PRIx32 ".ms"

const char *TAG = "MIDLTS         ";

typedef int32_t midlts_id_t;

typedef struct _midlts_ctx_t {
	midsess_ver_app_t app;
	size_t session_count;
	midlts_id_t active_id;
	size_t active_capacity;
	midsess_t *active;
} midlts_ctx_t;


typedef enum _midlts_err_t {
	LTS_OK = 0,
	LTS_FS = 1,
	LTS_CRC = 2,
	LTS_MEM = 3,
	LTS_INVALID = 4,
} midlts_err_t;

static uint32_t mid_session_calc_crc(midsess_t *sess) {
	uint32_t tmp = sess->sess_crc;
	sess->sess_crc = 0;
	uint32_t crc = esp_crc32_le(0, (uint8_t *)sess, MIDLTS_SIZE (sess->sess_count));
	sess->sess_crc = tmp;
	return crc;
}

static midlts_err_t mid_session_buf_init(midlts_ctx_t *ctx) {
	ctx->active_capacity = 32;
	ctx->active = calloc(1, MIDLTS_SIZE (ctx->active_capacity));
	if (!ctx->active) {
		ESP_LOGE(TAG, "Memory allocation failed!");
		return LTS_MEM;
	}
	return LTS_OK;
}

static midlts_err_t mid_session_buf_grow_bytes(midlts_ctx_t *ctx, size_t bytes) {
	while (bytes > MIDLTS_SIZE (ctx->active_capacity)) {
		ctx->active_capacity *= 2;
	}
	ctx->active = realloc(ctx->active, MIDLTS_SIZE (ctx->active_capacity));
	if (!ctx->active) {
		ESP_LOGE(TAG, "Memory allocation failed (%zu bytes)!", bytes);
		return LTS_MEM;
	}
	return LTS_OK;
}

static midlts_err_t mid_session_buf_grow(midlts_ctx_t *ctx, size_t entries) {
	return mid_session_buf_grow_bytes(ctx, MIDLTS_SIZE (entries));
}

static midlts_err_t mid_session_buf_clear(midlts_ctx_t *ctx) {
	memset(ctx->active, 0, MIDLTS_SIZE (ctx->active_capacity));
	return LTS_OK;
}

midlts_err_t mid_session_write(midlts_ctx_t *ctx, midlts_id_t id) {
	midlts_err_t ret = LTS_OK;

	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, id);

	// NOTE: Must use r+ if file exists, then overwrite data to avoid losing
	// data if crash happens between fopen and fwrite to flash (and sync data
	// to flash)
	//
	FILE *fp;
	struct stat st;
	if (stat(buf, &st) == 0) {
		fp = fopen(buf, "r+");
	} else {
		fp = fopen(buf, "w");
	}

	if (!fp) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Can't open %s!", id, buf);
		return LTS_FS;
	}

	// Renew checksums
	midsess_t *sess = ctx->active;
	sess->sess_crc = mid_session_calc_crc(sess);

	if (fwrite(sess, MIDLTS_SIZE (sess->sess_count), 1, fp) != 1) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Fail to read file %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	if (fflush(fp)) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Fail to flush %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	if (fsync(fileno(fp))) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Fail to sync %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

close:
	if (fclose(fp)) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Fail to close %s", id, buf);
		ret = LTS_FS;
	}

	return ret;
}

midlts_err_t mid_session_read(midlts_ctx_t *ctx, midlts_id_t id) {
	midlts_err_t ret = LTS_OK;

	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, id);

	FILE *fp = fopen(buf, "r");
	if (!fp) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Can't open %s!", id, buf);
		return LTS_FS;
	}

	struct stat st;
	if (fstat(fileno(fp), &st) < 0) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Can't stat %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	if (mid_session_buf_grow_bytes(ctx, st.st_size) != LTS_OK) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Can't grow buffer %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	if (fread(ctx->active, st.st_size, 1, fp) != 1) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Can't read session file %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	midsess_t *sess = ctx->active;

	if (st.st_size != MIDLTS_SIZE(sess->sess_count)) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Session file incorrect length %s", id, buf);
		ret = LTS_FS;
		goto close;
	}

	if (sess->sess_crc != mid_session_calc_crc(sess)) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Session %s header checksums don't match!", id, buf);
		ret = LTS_CRC;
		goto close;
	}

close:
	if (fclose(fp)) {
		ESP_LOGE(TAG, "MID Session %" PRIx32 ": Fail to close %s", id, buf);
		ret = LTS_FS;
	}

	if (ret != LTS_OK) {
		mid_session_buf_clear(ctx);
	}

	return ret;
}

midlts_err_t mid_session_sync(midlts_ctx_t *ctx, midlts_id_t id) {
	midsess_t *sess = ctx->active;
	if (mid_session_calc_crc(sess) == sess->sess_crc) {
		return LTS_OK;
	}
	midlts_err_t ret;
	if ((ret = mid_session_write(ctx, id) != LTS_OK)) {
		return ret;
	}
	return LTS_OK;
}

midlts_err_t mid_session_purge(midlts_ctx_t *ctx, midlts_id_t id) {
	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, id);

	if (remove(buf)) {
		return LTS_FS;
	}

	return LTS_OK;
}

midlts_err_t mid_session_dump(midlts_ctx_t *ctx) {
	midsess_t *sess = ctx->active;

	ESP_LOGI(TAG, "MID Session %" PRIx32 ":", ctx->active_id);
	ESP_LOGI(TAG, " -   CRC: %08" PRIX32, sess->sess_crc);
	ESP_LOGI(TAG, " -    ID: %s", sess->sess_id);
	ESP_LOGI(TAG, " -  Auth: %s", sess->sess_auth);
	ESP_LOGI(TAG, " -  Flag: %08" PRIX32, sess->sess_flag);
	ESP_LOGI(TAG, " - Count: %08" PRIX32, sess->sess_count);

	for (uint16_t i = 0; i < sess->sess_count; i++) {
		midsess_meter_val_t *val = &sess->sess_values[i];
		midsess_ver_mid_t mid = val->meter_vmid;
		midsess_ver_app_t app = val->meter_vapp;
		ESP_LOGI(TAG, " -   Value: %08" PRIX64 " / %08" PRIX32 " / %08" PRIX32 " / v%d.%d.%d / v%d.%d.%d.%d",
				val->meter_time, val->meter_value, val->meter_flag, mid.v1, mid.v2, mid.v3, app.v1, app.v2, app.v3, app.v4);
	}

	return LTS_OK;
}

bool mid_session_is_complete(midlts_ctx_t *ctx) {
	// Check if complete (has end meter reading)
	midsess_t *active = ctx->active;
	uint16_t count = active->sess_count;
	midsess_meter_val_t *values = active->sess_values;

	for (uint16_t i = 0; i < count; i++) {
		if (values[i].meter_flag & MV_FLAG_READING_END) {
			return true;
		}
	}

	return false;
}

bool mid_session_is_incomplete(midlts_ctx_t *ctx) {
	return !mid_session_is_complete(ctx);
}

midlts_err_t mid_session_write_active(midlts_ctx_t *ctx) {
	return mid_session_write(ctx, ctx->active_id);
}

midlts_err_t mid_session_read_active(midlts_ctx_t *ctx) {
	return mid_session_read(ctx, ctx->active_id);
}

midlts_err_t mid_session_sync_active(midlts_ctx_t *ctx) {
	return mid_session_sync(ctx, ctx->active_id);
}

midlts_err_t mid_session_init(midlts_ctx_t *ctx, midsess_ver_app_t vapp) {
	midlts_err_t ret = LTS_OK;

	memset(ctx, 0, sizeof (*ctx));

	DIR *dir = opendir(MIDLTS_DIR);
	if (!dir) {
		ESP_LOGE(TAG, "Failure to open dir %s", MIDLTS_DIR);
		return LTS_FS;
	}

	if (mid_session_buf_init(ctx) != LTS_OK) {
		ESP_LOGE(TAG, "Failure to allocate buffer");
		ret = LTS_MEM;
		goto close;
	}

	ctx->app = vapp;

	ctx->session_count = 0;
	ctx->active_id = -1;

	struct dirent *dp = NULL;
	while ((dp = readdir(dir)) != NULL) {
		midlts_id_t id;

		int ch;
		int scan = sscanf(dp->d_name, MIDLTS_SCN, &id, &ch);
		int len = strlen(dp->d_name);

		if (scan == 1 && len == ch && dp->d_type == DT_REG) {
			if (id > ctx->active_id) {
				ctx->active_id = id;
			}
			ctx->session_count++;
		}
	}

	mid_session_buf_clear(ctx);

	if (ctx->active_id >= 0) {
		midlts_err_t err = mid_session_read_active(ctx);
		if (err == LTS_OK) {
			if (mid_session_is_complete(ctx)) {
				// Last session is complete, write next entry to a new file
				ESP_LOGI(TAG, "MID LTS: Last session is complete");
				mid_session_buf_clear(ctx);
				ctx->active_id++;
			} else {
				// Last session is incomplete, continue this session
				ESP_LOGI(TAG, "MID LTS: Last session is incomplete");
			}
		} else {
			ESP_LOGE(TAG, "MID LTS: Last session is corrupt");
		}
	} else {
		ctx->active_id = 0;
		ESP_LOGI(TAG, "MID LTS: No sessions found");
	}

close:
	closedir(dir);
	return ret;
}

midlts_err_t mid_session_set_id(midlts_ctx_t *ctx, const char *id) {
	midsess_t *sess = ctx->active;
	strlcpy(sess->sess_id, id, sizeof (sess->sess_id));
	return LTS_OK;
}

midlts_err_t mid_session_set_auth(midlts_ctx_t *ctx, const char *auth) {
	midsess_t *sess = ctx->active;
	strlcpy(sess->sess_auth, auth, sizeof (sess->sess_auth));
	return LTS_OK;
}

// These two functions might not be needed if we can just use the start/end meter reading times
// but for now, I think this has to the same as the 'observedat' time of the first requesting observation
// sent to the cloud?
//
// TODO: Look into above ^
midlts_err_t mid_session_set_start_time(midlts_ctx_t *ctx, uint64_t tm_sec, uint32_t tm_usec) {
	midsess_t *sess = ctx->active;
	sess->sess_time_start.time_sec = tm_sec;
	sess->sess_time_start.time_usec = tm_usec;
	return LTS_OK;
}

midlts_err_t mid_session_set_end_time(midlts_ctx_t *ctx, uint64_t tm_sec, uint32_t tm_usec) {
	midsess_t *sess = ctx->active;
	sess->sess_time_end.time_sec = tm_sec;
	sess->sess_time_end.time_usec = tm_usec;
	return LTS_OK;
}

midlts_err_t mid_session_set_flag(midlts_ctx_t *ctx, midsess_flag_t flag) {
	midsess_t *sess = ctx->active;
	sess->sess_flag |= flag;
	return LTS_OK;
}

midlts_err_t mid_session_add_reading(midlts_ctx_t *ctx, midsess_meter_flag_t flag) {
	// TODO: Get real meter value
	// TODO: Get real time (update utz to store usecs)
	midsess_t *sess = ctx->active;

	// TODO: Check for double start/end just in case?
	if (mid_session_buf_grow(ctx, sess->sess_count + 1) != LTS_OK) {
		return LTS_MEM;
	}

	midsess_meter_val_t *val = &sess->sess_values[sess->sess_count++];
	val->meter_time = esp_random();
	val->meter_value = esp_random();
	val->meter_flag = flag;
	val->meter_vmid = (midsess_ver_mid_t) { 1, 2, 3 };
	val->meter_vapp = ctx->app;
	return LTS_OK;
}

#ifdef HOST

int main(int argc, char **argv) {
	midsess_ver_app_t app = { 9, 9, 9, 9 };

	midlts_ctx_t ctx;
	midlts_err_t ret = mid_session_init(&ctx, app);
	if (ret) {
		ESP_LOGE(TAG, "Failed to init MID sessions");
		return -1;
	}

	char c;
	while ((c = getopt(argc, argv, "dwsr:i:a:t:")) != -1) {
		switch (c) {
			case 'd':
				ret = mid_session_dump(&ctx);
				break;
			case 'r':
				ret = mid_session_read(&ctx, atoi(optarg));
				break;
			case 'w':
				ret = mid_session_write(&ctx, atoi(optarg));
				break;
			case 's':
				ret = mid_session_sync_active(&ctx);
				break;
			case 'i':
				ret = mid_session_set_id(&ctx, optarg);
				break;
			case 'a':
				ret = mid_session_set_auth(&ctx, optarg);
				break;
			case 't':
				if (optarg[0] == 's') {
					ret = mid_session_add_reading(&ctx, MV_FLAG_READING_START);
				} else if (optarg[0] == 'e') {
					ret = mid_session_add_reading(&ctx, MV_FLAG_READING_END);
				} else {
					ret = mid_session_add_reading(&ctx, MV_FLAG_READING_TARIFF);
				}
				break;
			case '?':
			default:
				break;
		}
	}

	ESP_LOGI(TAG, "Ret -> %d", ret);

	return 0;
}

#endif
