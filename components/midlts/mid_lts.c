#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include "mid_session.h"
#include "mid_lts.h"
#include "mid_lts_priv.h"

static const char *TAG = "MIDLTS         ";

static midlts_err_t mid_session_active_session_grow(midlts_ctx_t *ctx, size_t capacity) {
	//ESP_LOGI(TAG, "MID Active Session: Grow %zu", capacity);

	midlts_active_session_t *active = &ctx->active_session;
	active->events = realloc(active->events, sizeof (mid_session_meter_value_t) * capacity);

	if (!active) {
		return LTS_ALLOC;
	}

	active->capacity = capacity;
	return LTS_OK;
}

static midlts_err_t mid_session_active_session_alloc(midlts_ctx_t *ctx) {
	return mid_session_active_session_grow(ctx, 64);
}

static midlts_err_t mid_session_active_session_append(midlts_ctx_t *ctx, mid_session_meter_value_t *rec) {
	midlts_active_session_t *active = &ctx->active_session;

	if (active->count >= active->capacity) {
		midlts_err_t err;
		if ((err = mid_session_active_session_grow(ctx, active->capacity * 2)) != LTS_OK) {
			return err;
		}
	}

	//ESP_LOGI(TAG, "MID Active Session: Append %zu", active->count);
	active->events[active->count++] = *rec;

	active->has_versions = true;
	active->lr = rec->lr;
	active->fw = rec->fw;

	return LTS_OK;
}

static void mid_session_active_session_set_id(midlts_ctx_t *ctx, mid_session_id_t *id) {
	//ESP_LOGI(TAG, "MID Active Session: Set Id");
	midlts_active_session_t *active = &ctx->active_session;
	active->has_id = true;
	active->id = *id;
}

static void mid_session_active_session_set_auth(midlts_ctx_t *ctx, mid_session_auth_t *auth) {
	//ESP_LOGI(TAG, "MID Active Session: Set Auth");
	midlts_active_session_t *active = &ctx->active_session;
	active->has_auth = true;
	active->auth = *auth;
}

static void mid_session_active_session_reset(midlts_ctx_t *ctx) {
	//ESP_LOGI(TAG, "MID Active Session: Reset");
	midlts_active_session_t *active = &ctx->active_session;

	active->has_id = false;
	memset(&active->id, 0, sizeof (active->id));

	active->has_auth = false;
	memset(&active->auth, 0, sizeof (active->auth));

	active->has_versions = false;
	memset(&active->lr, 0, sizeof (active->lr));
	memset(&active->fw, 0, sizeof (active->fw));

	memset(active->events, 0, sizeof (mid_session_meter_value_t) * active->capacity);
	active->count = 0;
}

static uint32_t mid_session_calc_crc(mid_session_record_t *r) {
	mid_session_record_t rec = *r;
	rec.rec_crc = 0xFFFFFFFF;
	return esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));
}

static bool mid_session_check_crc(mid_session_record_t *r) {
	return r->rec_crc == mid_session_calc_crc(r);
}

static midlts_err_t mid_session_log_update_state(midlts_ctx_t *ctx, mid_session_record_t *rec) {
	if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
		if (rec->rec_type == MID_SESSION_RECORD_TYPE_ID) {
			mid_session_active_session_set_id(ctx, &rec->id);
		}

		if (rec->rec_type == MID_SESSION_RECORD_TYPE_AUTH) {
			mid_session_active_session_set_auth(ctx, &rec->auth);
		}
	}

	if (rec->rec_type != MID_SESSION_RECORD_TYPE_METER_VALUE) {
		return LTS_OK;
	}

	midlts_err_t ret;

	if (rec->meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
		if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
			return LTS_SESSION_ALREADY_OPEN;
		}

		// First clear active session in RAM
		mid_session_active_session_reset(ctx);

		if ((ret = mid_session_active_session_append(ctx, &rec->meter_value)) != LTS_OK) {
			return ret;
		}

		ctx->flags |= LTS_FLAG_SESSION_OPEN;
	} else if (rec->meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
		if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
			return LTS_SESSION_NOT_OPEN;
		}

		if ((ret = mid_session_active_session_append(ctx, &rec->meter_value)) != LTS_OK) {
			return ret;
		}

		ctx->flags &= ~LTS_FLAG_SESSION_OPEN;
	} else if (rec->meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF) {
		if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
			if ((ret = mid_session_active_session_append(ctx, &rec->meter_value)) != LTS_OK) {
				return ret;
			}
		}
	}

	return LTS_OK;
}

static midlts_err_t mid_session_log_get_latest_meter_value(midlts_ctx_t *ctx, midlts_id_t logid, bool *found_meter, mid_session_record_t *meter);

static midlts_err_t mid_session_log_purge(midlts_ctx_t *ctx) {
	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, ctx->msg_page);

	ESP_LOGI(TAG, "MID Session Delete  - %" PRIu32, ctx->msg_page);

	if (remove(buf) != 0) {
		return LTS_ERASE;
	}
	return LTS_OK;
}

static midlts_err_t mid_session_log_try_purge(midlts_ctx_t *ctx, time_t now_unix) {
	bool found = false;
	mid_session_record_t meter = {0};

	midlts_err_t err = mid_session_log_get_latest_meter_value(ctx, ctx->msg_page, &found, &meter);
	if (err != LTS_OK) {
		return err;
	}

	if (!found) {
		// Shouldn't really happen, there should always be at least 1 meter value in a log
		ESP_LOGI(TAG, "MID Session Purge   - %" PRIu32 " - No Meter Values", ctx->msg_page);
		mid_session_print_record(&meter);

		return mid_session_log_purge(ctx);
	}

	int32_t age = MID_TIME_PACK(now_unix) - meter.meter_value.time;

	if (age > MID_TIME_MAX_AGE) {
		ESP_LOGI(TAG, "MID Session Purge   - %" PRIu32 " - %f Days > Threshold", ctx->msg_page, (double)age / (24 * 60 * 60));
		mid_session_print_record(&meter);

		return mid_session_log_purge(ctx);
	}

	ESP_LOGI(TAG, "MID Session Purge   - %" PRIu32 " - %f Days < Threshold", ctx->msg_page, (double)age / (24 * 60 * 60));
	mid_session_print_record(&meter);

	return LTS_LOG_FILE_FULL;
}

static midlts_err_t mid_session_log_record_internal(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	midlts_err_t ret = LTS_OK;

	rec->rec_id = ctx->msg_id;
	rec->rec_crc = 0xFFFFFFFF;
	rec->rec_crc = esp_crc32_le(0, (uint8_t *)rec, sizeof (*rec));

	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, ctx->msg_page);

	FILE *fp = fopen(buf, "a");
	if (!fp) {
		return LTS_OPEN;
	}

	long size = ftell(fp);
	if (size < 0) {
		ret = LTS_TELL;
		goto close;
	}

	if (size + sizeof (*rec) > MIDLTS_LOG_MAX_SIZE) {
		ret = LTS_LOG_FILE_FULL;
		goto close;
	}

	if (fwrite(rec, sizeof (*rec), 1, fp) != 1) {
		ret = LTS_WRITE;
		goto close;
	}

	if (fflush(fp)) {
		ret = LTS_FLUSH;
		goto close;
	}

	if (fsync(fileno(fp))) {
		ret = LTS_SYNC;
		goto close;
	}

close:
	if (fclose(fp)) {
		return LTS_CLOSE;
	}

	if (ret == LTS_OK) {
		if ((ret = mid_session_log_update_state(ctx, rec)) != LTS_OK) {
			return ret;
		}

		mid_session_print_record(rec);

		if (pos) {
			pos->id = ctx->msg_page;
			pos->offset = size;
			pos->crc = rec->rec_crc;
		}

		ctx->msg_id++;
	}

	return ret;
}

static midlts_err_t mid_session_log_record(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_record_t *rec) {
	midlts_err_t err = mid_session_log_record_internal(ctx, pos, rec);

	if (err == LTS_LOG_FILE_FULL) {
		ctx->msg_page = (ctx->msg_page + 1) % ctx->max_pages;
		err = mid_session_log_record_internal(ctx, pos, rec);
		if (err == LTS_LOG_FILE_FULL) {
			err = mid_session_log_try_purge(ctx, now);
			if (err != LTS_OK) {
				return err;
			}
			err = mid_session_log_record_internal(ctx, pos, rec);
		}
	}

	return err;
}

static midlts_err_t mid_session_log_read_record(midlts_ctx_t *ctx, midlts_id_t logid, size_t offset, mid_session_record_t *rec) {
	midlts_err_t ret = LTS_OK;

	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, logid);

	struct stat st;
	if (stat(buf, &st)) {
		return LTS_STAT;
	}

	if (offset + sizeof (*rec) > st.st_size) {
		return LTS_STAT;
	}

	FILE *fp = fopen(buf, "r");
	if (!fp) {
		return LTS_OPEN;
	}

	if (fseek(fp, offset, SEEK_SET)) {
		return LTS_SEEK;
	}

	if (fread(rec, 1, sizeof (*rec), fp) != sizeof (*rec)) {
		ret = LTS_READ;
		goto close;
	}

	if (!mid_session_check_crc(rec)) {
		ret = LTS_BAD_CRC;
		goto close;
	}

close:
	if (fclose(fp)) {
		return LTS_CLOSE;
	}

	return ret;
}

static midlts_err_t mid_session_log_get_latest_meter_value(midlts_ctx_t *ctx, midlts_id_t logid, bool *found_meter, mid_session_record_t *meter) {
	midlts_err_t ret = LTS_OK;

	static uint8_t databuf[MIDLTS_LOG_MAX_SIZE];

	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, logid);

	struct stat st;
	if (stat(buf, &st) || st.st_size > sizeof (databuf)
			|| st.st_size % sizeof (mid_session_record_t) != 0) {
		return LTS_STAT;
	}

	FILE *fp = fopen(buf, "r");
	if (!fp) {
		return LTS_OPEN;
	}

	if (fread(databuf, 1, st.st_size, fp) != st.st_size) {
		ret = LTS_READ;
		goto close;
	}

	for (size_t i = 0; i < st.st_size; i += sizeof (mid_session_record_t)) {
		mid_session_record_t rec = *(mid_session_record_t *)&databuf[i];
		if (!mid_session_check_crc(&rec)) {
			ret = LTS_BAD_CRC;
			goto close;
		}
		if (rec.rec_type == MID_SESSION_RECORD_TYPE_METER_VALUE) {
			*found_meter = true;
			*meter = rec;
		}
	}

close:
	if (fclose(fp)) {
		return LTS_CLOSE;
	}

	return ret;
}

static midlts_err_t mid_session_log_replay(midlts_ctx_t *ctx, midlts_id_t logid, bool initial) {
	midlts_err_t ret = LTS_OK;

	static uint8_t databuf[MIDLTS_LOG_MAX_SIZE];

	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, logid);

	struct stat st;
	if (stat(buf, &st) || st.st_size > sizeof (databuf)
			|| st.st_size % sizeof (mid_session_record_t) != 0) {
		return LTS_STAT;
	}

	FILE *fp = fopen(buf, "r");
	if (!fp) {
		return LTS_OPEN;
	}

	if (fread(databuf, 1, st.st_size, fp) != st.st_size) {
		ret = LTS_READ;
		goto close;
	}

	bool first_record = true;

	for (size_t i = 0; i < st.st_size; i += sizeof (mid_session_record_t)) {
		mid_session_record_t rec = *(mid_session_record_t *)&databuf[i];

		if (!mid_session_check_crc(&rec)) {
			ret = LTS_BAD_CRC;
			goto close;
		}

		mid_session_print_record(&rec);

		if (initial && first_record) {
			ctx->msg_id = rec.rec_id;
		}

		// Ensure no messages go missing
		if (rec.rec_id != ctx->msg_id) {
			ret = LTS_MSG_OUT_OF_ORDER;
			goto close;
		} else {
			ctx->msg_id++;
		}

		if ((ret = mid_session_log_update_state(ctx, &rec)) != LTS_OK) {
			return ret;
		}

		first_record = false;
	}

close:
	if (fclose(fp)) {
		return LTS_CLOSE;
	}

	return ret;
}

static void mid_session_fill_meter_value(midlts_ctx_t *ctx, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter, mid_session_record_t *rec) {
	rec->rec_type = MID_SESSION_RECORD_TYPE_METER_VALUE;
	rec->meter_value.lr = ctx->lr_version;
	rec->meter_value.fw = ctx->fw_version;
	rec->meter_value.time = MID_TIME_PACK(now);
	rec->meter_value.flag = flag;
	rec->meter_value.meter = meter;
}

midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t rec = {0};
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_START, meter, &rec);

	if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
		return LTS_SESSION_ALREADY_OPEN;
	}

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if (out) {
		*out = rec;
	}
	return LTS_OK;
}

midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t rec = {0};
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF, meter, &rec);

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if (out) {
		*out = rec;
	}

	return err;
}

midlts_err_t mid_session_add_close(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = {0};
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_END, meter, &rec);

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if (out) {
		*out = rec;
	}
	return err;
}

// These two following functions can only add data to an open session
midlts_err_t mid_session_add_id(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, time_t now, uint8_t uuid[16]) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = {0};
	rec.rec_type = MID_SESSION_RECORD_TYPE_ID;
	memcpy(rec.id.uuid, uuid, sizeof (rec.id.uuid));

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if (out) {
		*out = rec;
	}
	return LTS_OK;
}

midlts_err_t mid_session_add_auth(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, time_t now, mid_session_auth_type_t type, uint8_t *data, size_t data_size) {
	mid_session_record_t rec = {0};

	if (data_size > sizeof (rec.auth.tag)) {
		return LTS_BAD_ARG;
	}

	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	rec.rec_type = MID_SESSION_RECORD_TYPE_AUTH;
	rec.auth.type = type;
	rec.auth.length = data_size;
	memcpy(rec.auth.tag, data, data_size);

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if (out) {
		*out = rec;
	}
	return LTS_OK;
}

midlts_err_t mid_session_set_purge_limit(midlts_ctx_t *ctx, midlts_pos_t *pos) {
	// TODO: Validate file exists?
	ctx->min_purgeable = *pos;
	return LTS_OK;
}

midlts_err_t mid_session_init_internal(midlts_ctx_t *ctx, size_t max_pages, time_t now, mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version) {
	midlts_err_t ret = LTS_OK;

	memset(ctx, 0, sizeof (*ctx));

	if ((ret = mid_session_active_session_alloc(ctx) != LTS_OK) != LTS_OK) {
		return ret;
	}

	ctx->lr_version = lr_version;
	ctx->fw_version = fw_version;

	ctx->max_pages = max_pages;

	midlts_id_t min_page = 0xFFFFFFFF;
	midlts_id_t max_page = 0xFFFFFFFF;
	midlts_id_t min_id = 0;
	midlts_id_t max_id = 0;

	for (midlts_id_t id = 0; id < ctx->max_pages; id++) {
		char buf[64];
		snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, id);

		struct stat st;
		if (stat(buf, &st)) {
			continue;
		}

		mid_session_record_t rec;
		if ((ret = mid_session_log_read_record(ctx, id, 0, &rec)) != LTS_OK) {
			return ret;
		}

		ESP_LOGI(TAG, "MID Session Replay  - %" PRIu32 " / First %" PRIu32, id, rec.rec_id);

		if (min_page == 0xFFFFFFFF) {
			min_page = max_page = id;
			min_id = max_id = rec.rec_id;
		}

		if (rec.rec_id < min_id) {
			min_id = rec.rec_id;
			min_page = id;
		}

		if (rec.rec_id > max_id) {
			max_id = rec.rec_id;
			max_page = id;
		}
	}

	if (min_page == 0xFFFFFFFF) {
		min_page = 0;
		max_page = 0;

		ESP_LOGI(TAG, "MID Session Replay  - Empty");

		return LTS_OK;
	}

	ESP_LOGI(TAG, "MID Session Replay  - Min %" PRIu32 " / Max %" PRIu32, min_page, max_page);

	midlts_id_t page = min_page;

	while (true) {
		ESP_LOGI(TAG, "MID Session Replay  - %" PRIu32, page);

		if ((ret = mid_session_log_replay(ctx, page, page == min_page)) != LTS_OK) {
			return ret;
		}

		if (page == max_page) {
			break;
		}

		page = (page + 1) % ctx->max_pages;
	}

	ctx->msg_page = max_page;

	return ret;
}

midlts_err_t mid_session_read_record(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	midlts_err_t err = mid_session_log_read_record(ctx, pos->id, pos->offset, rec);
	if (err != LTS_OK) {
		return err;
	}
	if (pos->crc != rec->rec_crc) {
		return LTS_BAD_CRC;
	}
	return LTS_OK;
}

// Functions below only for testing purposes
midlts_err_t mid_session_init(midlts_ctx_t *ctx, time_t now, mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version) {
	return mid_session_init_internal(ctx, MIDLTS_LOG_MAX_FILES, now, fw_version, lr_version);
}

void mid_session_free(midlts_ctx_t *ctx) {
	if (ctx->active_session.events) {
		free(ctx->active_session.events);
		memset(&ctx->active_session, 0, sizeof (ctx->active_session));
	}
}

midlts_err_t mid_session_reset_page(midlts_id_t id) {
	char buf[64];
	snprintf(buf, sizeof (buf), MIDLTS_DIR MIDLTS_PRI, id);
	remove(buf);
	return LTS_OK;
}

midlts_err_t mid_session_reset(void) {
	for (midlts_id_t i = 0; i < MIDLTS_LOG_MAX_FILES; i++) {
		mid_session_reset_page(i);
	}
	return LTS_OK;
}


