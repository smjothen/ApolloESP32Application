#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>

#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "mid_session.pb.h"
#include "mid_lts.h"
#include "mid_lts_priv.h"

static const char *TAG = "MIDLTS         ";

static void mid_session_record_update_stats(midlts_ctx_t *ctx, mid_session_record_t *rec) {
	ctx->last_message = rec->rec_id;
	ctx->stats.message_count++;

	if (rec->has_meter_value) {
		uint32_t flag = rec->meter_value.flag;
		if (flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
			ctx->stats.start_count++;
		} else if (flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
			ctx->stats.end_count++;
		} else if (flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF) {
			ctx->stats.tariff_count++;
		}
	}
}

static void mid_session_record_add_version(midlts_ctx_t *ctx, mid_session_record_t *rec) {
	if (strcmp(ctx->fw_version, ctx->current_file.fw_version.code)) {
		strlcpy(ctx->current_file.fw_version.code, ctx->fw_version, sizeof (rec->fw_version.code));
		rec->has_fw_version = true;
		rec->fw_version = ctx->current_file.fw_version;
	}

	if (strcmp(ctx->lr_version, ctx->current_file.lr_version.code)) {
		strlcpy(ctx->current_file.lr_version.code, ctx->lr_version, sizeof (rec->lr_version.code));
		rec->has_lr_version = true;
		rec->lr_version = ctx->current_file.lr_version;
	}
}

static midlts_err_t mid_session_log_record_internal(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	midlts_err_t ret = LTS_OK;

	pb_byte_t databuf[MID_SESSION_RECORD_SIZE];
	pb_ostream_t stream = pb_ostream_from_buffer(databuf, sizeof (databuf));

	// Ensure each file has version information for future meter value
	mid_session_record_add_version(ctx, rec);

	rec->rec_crc = 0xFFFFFFFF;
	rec->rec_crc = esp_crc32_le(0, (uint8_t *)rec, sizeof (*rec));

	mid_session_print_record(rec);

	if (!pb_encode_delimited(&stream, MID_SESSION_RECORD_FIELDS, rec)) {
		ESP_LOGE(TAG, "Error encoding protobuf: %s", PB_GET_ERROR(&stream));
		return LTS_PROTO_ENCODE;
	}

	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, ctx->log_id);

	FILE *fp = fopen(buf, "a");
	if (!fp) {
		return LTS_OPEN;
	}

	long size = ftell(fp);
	if (size < 0) {
		ret = LTS_TELL;
		goto close;
	}

	if (size + stream.bytes_written > MIDLTS_LOG_MAX_SIZE) {
		ret = LTS_LOG_FILE_FULL;
		goto close;
	}

	if (fwrite(databuf, stream.bytes_written, 1, fp) != 1) {
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
		mid_session_record_update_stats(ctx, rec);

		pos->id = ctx->log_id;
		pos->off = size;

		//ESP_LOGI(TAG, "MID Session Pos     - %" PRId16 "-%" PRId16, pos->id, pos->off);
	}

	return ret;
}

static midlts_err_t mid_session_log_replay(midlts_ctx_t *ctx, midlts_id_t logid, bool initial);
static midlts_err_t mid_session_count_files(uint32_t *count, midlts_id_t *min, midlts_id_t *max);

midlts_err_t mid_session_purge(midlts_pos_t *pos, time_t now) {
	char buf[64];

	uint32_t count;
	midlts_id_t min_id, max_id;
	midlts_err_t err = mid_session_count_files(&count, &min_id, &max_id);
	if (err != LTS_OK) {
		return err;
	}

	// Try to purge if we have > 16 files in total
	if (count > 16) {
		ESP_LOGI(TAG, "MID Session Purge   - %" PRIu32 " Storage %" PRIu32, min_id, pos->id);

		// Check if first file can be purged
		midlts_ctx_t ctx_purge = {0};
		ctx_purge.stats.latest = -1;

		if ((err = mid_session_log_replay(&ctx_purge, min_id, true)) != LTS_OK) {
			return err;
		}

		time_t latest = ctx_purge.stats.latest;

		// TODO: Check `now' is accurate (NTP sync recently?)
		if (latest > 0 && latest < now - MIDLTS_LOG_MAX_AGE && min_id < pos->id) {
			ESP_LOGI(TAG, "MID Session Purge   - %" PRIu32 " - Latest %" PRId64 " < Now %" PRId64 " - Limit %" PRId64,
					min_id, latest, now, MIDLTS_LOG_MAX_AGE);

			sprintf(buf, MIDLTS_DIR MIDLTS_PRI, min_id);

			if (remove(buf) != 0) {
				return LTS_REMOVE;
			}
		}
	}

	return LTS_OK;
}

static midlts_err_t mid_session_log_record(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_record_t *rec) {
	uint32_t count;
	midlts_id_t min_id, max_id;
	midlts_err_t err;

	rec->rec_id = ctx->msg_id;

	if ((err = mid_session_log_record_internal(ctx, pos, rec)) == LTS_LOG_FILE_FULL) {
		// Clear file specific context
		memset(&ctx->current_file, 0, sizeof (ctx->current_file));

		// Try purging earliest file if possible!
		err = mid_session_purge(&ctx->min_purgeable, now);
		if (err != LTS_OK) {
			return err;
		}
		err = mid_session_count_files(&count, &min_id, &max_id);
		if (err != LTS_OK) {
			return err;
		}
		if (count + 1 > MIDLTS_LOG_MAX_FILES) {
			return LTS_FS_FULL;
		}

		// Roll over to next log file
		ctx->log_id++;
		err = mid_session_log_record_internal(ctx, pos, rec);
	}

	if (err == LTS_OK) {
		ctx->msg_id++;
	}

	return err;
}

static midlts_err_t mid_session_log_replay(midlts_ctx_t *ctx, midlts_id_t logid, bool initial) {
	midlts_err_t ret = LTS_OK;

	static pb_byte_t databuf[MIDLTS_LOG_MAX_SIZE];

	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, logid);

	struct stat st;
	if (stat(buf, &st)) {
		return LTS_STAT;
	}

	if (st.st_size > sizeof (databuf)) {
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

	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;

	pb_istream_t stream = pb_istream_from_buffer(databuf, st.st_size);
	bool first_record = true;

	while (stream.bytes_left > 0) {
		if (!pb_decode_delimited(&stream, MID_SESSION_RECORD_FIELDS, &rec)) {
			ESP_LOGE(TAG, "Error decoding protobuf: %s", PB_GET_ERROR(&stream));
			ret = LTS_PROTO_DECODE;
			goto close;
		}

		uint32_t crc = rec.rec_crc;
		rec.rec_crc = 0xFFFFFFFF;
		uint32_t crc2 = esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));
		rec.rec_crc = crc;

		if (crc != crc2) {
			ret = LTS_BAD_CRC;
			goto close;
		}

		if (ctx->flags & LTS_FLAG_REPLAY_PRINT) {
			mid_session_print_record(&rec);
		}

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

		if (rec.has_fw_version) {
			ctx->current_file.fw_version = rec.fw_version;
		}

		if (rec.has_lr_version) {
			ctx->current_file.lr_version = rec.lr_version;
		}

		if (rec.has_meter_value) {
			uint32_t flag = rec.meter_value.flag;
			time_t time = rec.meter_value.time;

			if (time > ctx->stats.latest) {
				ctx->stats.latest = time;
			}

			if (flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
				if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
					ret = LTS_SESSION_ALREADY_OPEN;
					goto close;
				} else {
					ctx->flags |= LTS_FLAG_SESSION_OPEN;
				}
			}

			// First file may contain tail of previous session (no session start, but session end)
			// which we allow, but only in the first (initial) file!
			if (flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
				if (!initial && !(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
					ret = LTS_SESSION_NOT_OPEN;
					goto close;
				} else {
					ctx->flags &= ~LTS_FLAG_SESSION_OPEN;
				}
			}
		}

		mid_session_record_update_stats(ctx, &rec);

		first_record = false;
	}

close:
	if (fclose(fp)) {
		return LTS_CLOSE;
	}

	return ret;
}

midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16], mid_session_meter_value_flag_t flag, uint32_t meter) {
	if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
		return LTS_SESSION_ALREADY_OPEN;
	}

	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;
	rec.has_id = true;
	memcpy(rec.id.uuid, uuid, sizeof (rec.id.uuid));
	rec.has_meter_value = true;
	rec.meter_value.time = now;
	rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_START;
	rec.meter_value.meter = meter;

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) == LTS_OK) {
		ctx->flags |= LTS_FLAG_SESSION_OPEN;
	}

	return err;
}

midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;
	rec.has_meter_value = true;
	rec.meter_value.time = now;
	rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	rec.meter_value.meter = meter;

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) == LTS_OK) {
	}

	return err;
}

midlts_err_t mid_session_add_close(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;
	rec.has_meter_value = true;
	rec.meter_value.time = now;
	rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_END;
	rec.meter_value.meter = meter;

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) == LTS_OK) {
		ctx->flags &= ~LTS_FLAG_SESSION_OPEN;
	}

	return err;
}

midlts_err_t mid_session_add_id(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16]) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;
	rec.has_id = true;
	memcpy(rec.id.uuid, uuid, sizeof (rec.id.uuid));
	return mid_session_log_record(ctx, pos, now, &rec);
}

midlts_err_t mid_session_add_auth(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_auth_type_t type, uint8_t *data, size_t data_size) {
	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;

	if (data_size > sizeof (rec.auth.tag.bytes)) {
		return LTS_BAD_ARG;
	}

	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	rec.has_auth = true;
	rec.auth.type = type;
	rec.auth.tag.size = data_size;
	memcpy(rec.auth.tag.bytes, data, data_size);
	return mid_session_log_record(ctx, pos, now, &rec);
}

midlts_err_t mid_session_set_purge_limit(midlts_ctx_t *ctx, midlts_pos_t *pos) {
	// TODO: Validate file exists?
	ctx->min_purgeable = *pos;
	return LTS_OK;
}

static midlts_err_t mid_session_count_files(uint32_t *count, midlts_id_t *min, midlts_id_t *max) {
	*count = 0;
	*min = 0xFFFFFFFF;
	*max = 0;

	DIR *dir = opendir(MIDLTS_DIR);
	if (!dir) {
		ESP_LOGI(TAG, "Failure to open dir %s", MIDLTS_DIR);
		return LTS_OPENDIR;
	}

	struct dirent *dp = NULL;
	while ((dp = readdir(dir)) != NULL) {
		midlts_id_t id;

		int ch;
		int scan = sscanf(dp->d_name, MIDLTS_SCN, &id, &ch);
		int len = strlen(dp->d_name);

		if (scan == 1 && len == ch && dp->d_type == DT_REG) {
			(*count)++;

			if (id > *max) {
				*max = id;
			}

			if (id < *min) {
				*min = id;
			}
		}
	}

	closedir(dir);
	return LTS_OK;
}

midlts_err_t mid_session_init(midlts_ctx_t *ctx, time_t now, const char *fw_version, const char *lr_version) {
	memset(ctx, 0, sizeof (*ctx));

	uint32_t count = 0;
	midlts_id_t min_id, max_id;
	midlts_err_t ret = mid_session_count_files(&count, &min_id, &max_id);
	if (ret != LTS_OK) {
		return ret;
	}

	ctx->min_purgeable = MIDLTS_POS_MAX;

	ctx->lr_version = lr_version;
	ctx->fw_version = fw_version;

	ctx->msg_id = 0;
	ctx->log_id = 0;

	if (!count) {
		return LTS_OK;
	}

	struct stat st;
	char buf[64];

	for (midlts_id_t id = min_id; id <= max_id; id++) {
		// If a file goes missing that is a problem, don't allow initialization
		sprintf(buf, MIDLTS_DIR MIDLTS_PRI, id);
		if (stat(buf, &st)) {
			return LTS_STAT;
		}

		ESP_LOGI(TAG, "MID Session Replay  - %" PRId32, id);

		// Clear file specific context
		memset(&ctx->current_file, 0, sizeof (ctx->current_file));

		if ((ret = mid_session_log_replay(ctx, id, id == min_id)) != LTS_OK) {
			return ret;
		}

		MIDLTS_YIELD;
	}

	ctx->log_id = max_id;

	ESP_LOGI(TAG, "MID Session Restore - Log %" PRId32 " Message %" PRIx32 " Flags %" PRIx32, ctx->log_id, ctx->msg_id, ctx->flags);
	ESP_LOGI(TAG, "MID Session Restore - Total %" PRId32 " Start %" PRId32 " End %" PRId32 " Tariff %" PRId32,
			ctx->stats.message_count, ctx->stats.start_count, ctx->stats.end_count, ctx->stats.tariff_count);

	return ret;
}

midlts_err_t mid_session_get_record(midlts_pos_t *pos, mid_session_record_t *out) {
	midlts_err_t ret = LTS_OK;

	pb_byte_t databuf[MID_SESSION_RECORD_SIZE] = {0};

	char buf[64];
	sprintf(buf, MIDLTS_DIR MIDLTS_PRI, pos->id);

	FILE *fp = fopen(buf, "r");
	if (!fp) {
		return LTS_EOF;
	}

	struct stat st;
	if (fstat(fileno(fp), &st)) {
		ret = LTS_STAT;
		goto close;
	}

	size_t file_size = st.st_size;

	if (file_size == 0) {
		ret = LTS_EOF;
		goto close;
	}

	if (fseek(fp, pos->off, SEEK_SET) < 0) {
		ret = LTS_SEEK;
		goto close;
	}

	size_t read = fread(&databuf, 1, MID_SESSION_RECORD_SIZE, fp);
	if (read <= 0) {
		ret = LTS_READ;
		goto close;
	}

	mid_session_record_t rec = MID_SESSION_RECORD_INIT_DEFAULT;
	pb_istream_t stream = pb_istream_from_buffer(databuf, read);

	if (!pb_decode_delimited(&stream, MID_SESSION_RECORD_FIELDS, &rec)) {
		ESP_LOGE(TAG, "Error decoding protobuf: %s", PB_GET_ERROR(&stream));
		ret = LTS_PROTO_DECODE;
		goto close;
	} else {
		uint32_t crc = rec.rec_crc;
		rec.rec_crc = 0xFFFFFFFF;
		uint32_t crc2 = esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));

		if (crc != crc2) {
			ret = LTS_BAD_CRC;
			goto close;
		}

		size_t record_size = read - stream.bytes_left;

		pos->off += record_size;
		if (pos->off >= file_size) {
			pos->id++;
			pos->off = 0;
		}

		*out = rec;
	}

close:
	if (fclose(fp)) {
		ret = LTS_CLOSE;
	}

	return ret;
}

#ifdef HOST

#define TEST

uint8_t *fill_rand_bytes(uint8_t *out, size_t size) {
	for (size_t i = 0; i < size; i++) {
		out[i] = esp_random();
	}
	return out;
}

int main(int argc, char **argv) {
	midlts_ctx_t ctx;
	midlts_err_t err;
	time_t time = 0;

	if ((err = mid_session_init(&ctx, time, "2.0.0.406", "v1.2.8")) != LTS_OK) {
		ESP_LOGE(TAG, "Couldn't init MID session log! Error: %s", mid_session_err_to_string(err));
		return -1;
	}

	uint8_t buf[64] = {0};

	mid_session_meter_value_flag_t flags[] = {
		MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN,
		MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE,
		MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED,
		MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE,
		MID_SESSION_METER_VALUE_FLAG_METER_ERROR,
	};

	mid_session_auth_type_t types[] = {
		MID_SESSION_AUTH_TYPE_CLOUD,
		MID_SESSION_AUTH_TYPE_RFID,
		MID_SESSION_AUTH_TYPE_BLE,
		MID_SESSION_AUTH_TYPE_ISO15118,
		MID_SESSION_AUTH_TYPE_NEXTGEN,
	};


#ifdef TEST
	char c;
	while ((c = getopt (argc, argv, "i")) != -1) {
		switch (c) {
			case 'i':
				// Init only
				return 0;
		}
	}

	midlts_pos_t pos;

	for (int i = 0; i < 128; i++) {
		uint32_t flag = esp_random() % 5;
		switch (flag) {
			case 0: {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_open(&ctx, &pos, time, fill_rand_bytes(buf, 16), bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending session open : %s", mid_session_err_to_string(err));
				}
				break;
			}
			case 1: {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_close(&ctx, &pos, time, bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending session close : %s", mid_session_err_to_string(err));
				}
				break;
			}
			case 2: {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_tariff(&ctx, &pos, time, bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending tariff : %s", mid_session_err_to_string(err));
				}
				break;
			}
			case 3: {
				uint32_t size = esp_random() % 16;
				if (!size) {
					size = 1;
				}
				if ((err = mid_session_add_auth(&ctx, &pos, time, types[esp_random() % 5], fill_rand_bytes(buf, size), size)) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session auth : %s", mid_session_err_to_string(err));
				}
				break;
			}
			case 4: {
				uint32_t size = 16;
				if ((err = mid_session_add_id(&ctx, &pos, time, fill_rand_bytes(buf, size))) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session id : %s", mid_session_err_to_string(err));
				}
				break;
			}
			default:
				break;
		}
	}
#else
	char c;
	while ((c = getopt (argc, argv, "octai")) != -1) {
		switch (c) {
			case 'o': {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_open(&ctx, &pos, fill_rand_bytes(buf, 16), time(NULL), bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending session open : %s", mid_session_err_to_string(err));
					return -1;
				}
				break;
			}
			case 'c': {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_close(&ctx, &pos, time(NULL), bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending session close : %s", mid_session_err_to_string(err));
					return -1;
				}
				break;
			}
			case 't': {
				uint32_t bit = flags[esp_random() % 5];
				if ((err = mid_session_add_tariff(&ctx, &pos, time(NULL), bit, esp_random() % 4096)) != LTS_OK) {
					ESP_LOGE(TAG, "Error appending tariff : %s", mid_session_err_to_string(err));
					return -1;
				}
				break;
			}
			case 'a': {
				uint32_t size = esp_random() % 16;
				if (!size) {
					size = 1;
				}
				if ((err = mid_session_add_auth(&ctx, &pos, types[esp_random() % 5], fill_rand_bytes(buf, size), size)) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session auth : %s", mid_session_err_to_string(err));
					return -1;
				}
				break;
			}
			case 'i': {
				uint32_t size = 16;
				if ((err = mid_session_add_id(&ctx, &pos, fill_rand_bytes(buf, size))) != LTS_OK) {
					ESP_LOGE(TAG, "Couldn't log session id : %s", mid_session_err_to_string(err));
					return -1;
				}
				break;
			}
			case '?':
			default:
				break;
		}
	}
#endif

	return 0;
}

#endif
