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

static midlts_err_t mid_session_log_update_state(midlts_ctx_t *ctx, mid_session_record_t *rec) {
	if (rec->rec_type != MID_SESSION_RECORD_TYPE_METER_VALUE) {
		return LTS_OK;
	}

	if (rec->meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
		if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
			return LTS_SESSION_ALREADY_OPEN;
		}
		ctx->flags |= LTS_FLAG_SESSION_OPEN;
	} else if (rec->meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
		if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
			return LTS_SESSION_NOT_OPEN;
		}
		ctx->flags &= ~LTS_FLAG_SESSION_OPEN;
	}

	return LTS_OK;
}

static midlts_err_t mid_session_log_record_internal(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	size_t flash_size = ((esp_partition_t *)ctx->partition)->size;

	rec->rec_id = ctx->msg_id;
	rec->rec_crc = 0xFFFFFFFF;
	rec->rec_crc = esp_crc32_le(0, (uint8_t *)rec, sizeof (*rec));

	mid_session_print_record(rec);

	// Should never happen because we double check in public functions that sessions aren't
	// already open if we wan't to open, etc.
	midlts_err_t ret;
	if ((ret = mid_session_log_update_state(ctx, rec)) != LTS_OK) {
		return ret;
	}

	esp_err_t err = esp_partition_write(ctx->partition, ctx->msg_addr, rec, sizeof (*rec));
	if (err != ESP_OK) {
		return LTS_WRITE;
	}

	ctx->msg_addr = (ctx->msg_addr + sizeof (*rec)) % flash_size;
	ctx->msg_id++;

	return LTS_OK;
}

static midlts_err_t mid_session_log_record(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_record_t *rec) {
	midlts_err_t ret;

	if (ctx->msg_addr % FLASH_PAGE_SIZE == 0) {
		// TODO: Verify data is old enough
		// First write to page, need to erase
		//ESP_LOGI(TAG, "MID Session Erase - Erase %" PRId32, ctx->msg_addr);
		esp_err_t err = esp_partition_erase_range(ctx->partition, ctx->msg_addr, FLASH_PAGE_SIZE);
		if (err != ESP_OK) {
			return LTS_REMOVE;
		}

		mid_session_record_t lr = {0};
		lr.rec_type = MID_SESSION_RECORD_TYPE_LR_VERSION;
		strlcpy(lr.lr_version.code, ctx->lr_version, sizeof (lr.lr_version.code));

		if ((ret = mid_session_log_record_internal(ctx, pos, &lr)) != LTS_OK) {
			return ret;
		}

		mid_session_record_t fw = {0};
		fw.rec_type = MID_SESSION_RECORD_TYPE_FW_VERSION;
		strlcpy(fw.fw_version.code, ctx->fw_version, sizeof (fw.fw_version.code));

		if ((ret = mid_session_log_record_internal(ctx, pos, &fw)) != LTS_OK) {
			return ret;
		}
	}

	if ((ret = mid_session_log_record_internal(ctx, pos, rec)) != LTS_OK) {
		return ret;
	}

	return LTS_OK;
}

midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16], mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t id_rec = {0};
	id_rec.rec_type = MID_SESSION_RECORD_TYPE_ID;
	memcpy(id_rec.id.uuid, uuid, sizeof (id_rec.id.uuid));

	mid_session_record_t mv_rec = {0};
	mv_rec.rec_type = MID_SESSION_RECORD_TYPE_METER_VALUE;
	mv_rec.meter_value.time = now;
	mv_rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_START;
	mv_rec.meter_value.meter = meter;

	if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
		return LTS_SESSION_ALREADY_OPEN;
	}

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &mv_rec)) != LTS_OK) {
		return err;
	}
	if ((err = mid_session_log_record(ctx, pos, now, &id_rec)) != LTS_OK) {
		return err;
	}

	return LTS_OK;
}

midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t rec = {0};
	rec.rec_type = MID_SESSION_RECORD_TYPE_METER_VALUE;
	rec.meter_value.time = now;
	rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	rec.meter_value.meter = meter;

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	return err;
}

midlts_err_t mid_session_add_close(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = {0};
	rec.rec_type = MID_SESSION_RECORD_TYPE_METER_VALUE;
	rec.meter_value.time = now;
	rec.meter_value.flag = flag | MID_SESSION_METER_VALUE_READING_FLAG_END;
	rec.meter_value.meter = meter;

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	return err;
}

midlts_err_t mid_session_add_id(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16]) {
	if (!(ctx->flags & LTS_FLAG_SESSION_OPEN)) {
		return LTS_SESSION_NOT_OPEN;
	}

	mid_session_record_t rec = {0};
	rec.rec_type = MID_SESSION_RECORD_TYPE_ID;
	memcpy(rec.id.uuid, uuid, sizeof (rec.id.uuid));
	return mid_session_log_record(ctx, pos, now, &rec);
}

midlts_err_t mid_session_add_auth(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_auth_type_t type, uint8_t *data, size_t data_size) {
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
	return mid_session_log_record(ctx, pos, now, &rec);
}

midlts_err_t mid_session_set_purge_limit(midlts_ctx_t *ctx, midlts_pos_t *pos) {
	// TODO: Validate file exists?
	ctx->min_purgeable = *pos;
	return LTS_OK;
}

static midlts_err_t mid_session_init_partition(midlts_ctx_t *ctx, const esp_partition_t *partition, size_t flash_size, time_t now, const char *fw_version, const char *lr_version) {
	midlts_err_t ret = LTS_OK;

	if (flash_size % FLASH_PAGE_SIZE != 0) {
		return LTS_READ;
	}

	int16_t min_page = -1;
	midlts_id_t min_id = 0xFFFFFFFF;

	int16_t max_page = -1;
	midlts_id_t max_id = 0;

	memset(ctx, 0, sizeof (*ctx));

	ctx->partition = partition;
	ctx->num_pages = flash_size / FLASH_PAGE_SIZE;

	ctx->fw_version = fw_version;
	ctx->lr_version = lr_version;

	bool first = true;
	ctx->msg_id = 0;

	uint8_t data[sizeof (mid_session_record_t)];
	static uint8_t page[FLASH_PAGE_SIZE];

	size_t flash_pages = partition->size / FLASH_PAGE_SIZE;

	for (uint16_t i = 0; i < flash_pages; i++) {
		esp_err_t err = esp_partition_read_raw(ctx->partition, i * FLASH_PAGE_SIZE, page, sizeof (page));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		bool empty = true;
		for (size_t j = 0; j < sizeof (page); j++) {
			if (page[j] != 0xff) {
				empty = false;
				break;
			}
		}

		if (empty) {
			continue;
		}

		// Read raw, do no decryption to detect empty pages
		err = esp_partition_read(ctx->partition, i * FLASH_PAGE_SIZE, data, sizeof (data));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		mid_session_record_t rec = *(mid_session_record_t *)data;

		uint32_t crc = rec.rec_crc;
		rec.rec_crc = 0xFFFFFFFF;
		uint32_t crc2 = esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));
		rec.rec_crc = crc;

		if (crc != crc2) {
			return LTS_BAD_CRC;
		}

		if (min_page < 0) {
			min_page = max_page = i;
			min_id = max_id = rec.rec_id;
		}

		if (rec.rec_id > max_id) {
			max_id = rec.rec_id;
			max_page = i;
		}

		if (rec.rec_id < min_id) {
			min_id = rec.rec_id;
			min_page = i;
		}

		ESP_LOGI(TAG, "MID Session Recovery - Page %" PRId16 " - Valid - ID %" PRIu32, i, rec.rec_id);
	}

	if (min_page < 0) {
		// Flash is empty
		return LTS_OK;
	}

	//ESP_LOGI(TAG, "MID Session Recovery - Min Page %" PRId16 " - Max Page %" PRId16, min_page, max_page);
	//ESP_LOGI(TAG, "MID Session Recovery - Min Id %" PRId32 " - Max Id %" PRId32, min_id, max_id);

	uint16_t active_page = min_page;
	midlts_id_t active_id = min_id;
	bool active_initial = true;

	while (true) {
		esp_err_t err = esp_partition_read(ctx->partition, active_page * FLASH_PAGE_SIZE, page, sizeof (page));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		// Find current offset
		size_t i;
		for (i = 0; i < FLASH_PAGE_SIZE; i += sizeof (mid_session_record_t)) {
			mid_session_record_t rec = *(mid_session_record_t *)(page + i);
			mid_session_print_record(&rec);

			uint32_t crc = rec.rec_crc;
			rec.rec_crc = 0xFFFFFFFF;
			uint32_t crc2 = esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));
			rec.rec_crc = crc;

			if (crc != crc2) {
				ret = LTS_BAD_CRC;
				break;
			}

			if (first) {
				active_id = rec.rec_id;
				first = false;
			} else if (rec.rec_id != active_id + 1) {
				return LTS_MSG_OUT_OF_ORDER;
			} else {
				active_id++;
			}

			if ((ret = mid_session_log_update_state(ctx, &rec)) != LTS_OK) {
				if (active_initial && ret == LTS_SESSION_NOT_OPEN) {
					// First page may have tail of a session, we will allow this
					active_initial = false;
				} else {
					break;
				}
			}
		}

		// TODO: If bad CRC is detected, we could allow it if it's the last
		err = esp_partition_read_raw(ctx->partition, active_page * FLASH_PAGE_SIZE + i, page, FLASH_PAGE_SIZE - i);
		if (err != LTS_OK) {
			return LTS_READ;
		}

		bool empty = true;
		for (size_t j = 0; j < FLASH_PAGE_SIZE - i; j++) {
			if (page[j] != 0xff) {
				empty = false;
				break;
			}
		}

		if (empty) {
			// Remainer is empty, this is fine
			ret = LTS_OK;
			ctx->msg_id = active_id + 1;
			ctx->msg_addr = active_page * FLASH_PAGE_SIZE + i;
		} else {
			// TODO: Remainder has a partial write, need to recover or fail initialization
			ret = LTS_CORRUPT;
			break;
		}

		ESP_LOGI(TAG, "MID Session Recovery - Page %" PRId16 " / Id %" PRIu32 " / Addr %" PRIu32, active_page, active_id, ctx->msg_addr);

		if (active_page == max_page) {
			break;
		}

		active_page = (active_page + 1) % flash_pages;
	}

	return ret;
}

midlts_err_t mid_session_init(midlts_ctx_t *ctx, time_t now, const char *fw_version, const char *lr_version) {
	const esp_partition_t *partition;
	if ((partition = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "mid")) == NULL) {
		return LTS_READ;
	}
	return mid_session_init_partition(ctx, partition, partition->size, now, fw_version, lr_version);
}

midlts_err_t mid_session_reset(void) {
	const esp_partition_t *partition;
	if ((partition = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "mid")) == NULL) {
		return LTS_READ;
	}
	esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
	if (err != ESP_OK) {
		return LTS_REMOVE;
	}
	return LTS_OK;
}
