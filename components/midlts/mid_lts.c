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

static uint32_t mid_session_calc_crc(mid_session_record_t *r) {
	mid_session_record_t rec = *r;
	rec.rec_crc = 0xFFFFFFFF;
	rec.rec_status = 0xF;
	return esp_crc32_le(0, (uint8_t *)&rec, sizeof (rec));
}

static bool mid_session_check_crc(mid_session_record_t *r) {
	return r->rec_crc == mid_session_calc_crc(r);
}

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

static midlts_err_t mid_session_log_erase(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	esp_err_t err;
	size_t flash_size = ((esp_partition_t *)ctx->partition)->size;

	mid_session_print_record(rec);

	if (ctx->msg_addr % FLASH_PAGE_SIZE == 0) {
		// TODO: Verify data is old enough
		err = esp_partition_erase_range(ctx->partition, ctx->msg_addr, FLASH_PAGE_SIZE);
		if (err != ESP_OK) {
			return LTS_ERASE;
		}
	}

	err = esp_partition_write(ctx->partition, ctx->msg_addr, rec, sizeof (*rec));
	if (err != ESP_OK) {
		return LTS_WRITE;
	}

	// Could just turn verification of SPI writes on in the IDF config but ...
	uint8_t buf[sizeof (*rec)];
	err = esp_partition_read(ctx->partition, ctx->msg_addr, buf, sizeof (buf));
	if (err != ESP_OK) {
		return LTS_READ;
	}

	if (memcmp(buf, rec, sizeof (buf)) != 0) {
		return LTS_BAD_CRC;
	}

	if (pos) {
		pos->loc = ctx->msg_addr;
		pos->id = rec->rec_id;
	}

	ctx->msg_addr = (ctx->msg_addr + sizeof (*rec)) % flash_size;
	ctx->msg_id++;

	midlts_err_t ret;
	// Should never happen because we double check in public functions that sessions aren't
	// already open if we wan't to open, etc.
	if ((ret = mid_session_log_update_state(ctx, rec)) != LTS_OK) {
		return ret;
	}

	return LTS_OK;
}

static midlts_err_t mid_session_log_record(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_record_t *rec) {
	midlts_err_t ret;

	rec->rec_status = 0xF;
	rec->rec_id = ctx->msg_id;
	rec->rec_crc = 0xFFFFFFFF;
	rec->rec_crc = esp_crc32_le(0, (uint8_t *)rec, sizeof (*rec));

	if ((ret = mid_session_log_erase(ctx, pos, rec)) != LTS_OK) {
		return ret;
	}

	return LTS_OK;
}

static void mid_session_fill_meter_value(midlts_ctx_t *ctx, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter, mid_session_record_t *rec) {
	rec->rec_type = MID_SESSION_RECORD_TYPE_METER_VALUE;
	rec->meter_value.lr = ctx->lr_version;
	rec->meter_value.fw = ctx->fw_version;
	rec->meter_value.time = MID_TIME_PACK(now);
	rec->meter_value.flag = flag;
	rec->meter_value.meter = meter;
}

midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16], mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t id_rec = {0};
	id_rec.rec_type = MID_SESSION_RECORD_TYPE_ID;
	memcpy(id_rec.id.uuid, uuid, sizeof (id_rec.id.uuid));

	mid_session_record_t rec = {0};
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_START, meter, &rec);

	if (ctx->flags & LTS_FLAG_SESSION_OPEN) {
		return LTS_SESSION_ALREADY_OPEN;
	}

	midlts_err_t err;
	if ((err = mid_session_log_record(ctx, pos, now, &rec)) != LTS_OK) {
		return err;
	}

	if ((err = mid_session_log_record(ctx, pos, now, &id_rec)) != LTS_OK) {
		return err;
	}

	return LTS_OK;
}

midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter) {
	mid_session_record_t rec = {0};
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF, meter, &rec);

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
	mid_session_fill_meter_value(ctx, now, flag | MID_SESSION_METER_VALUE_READING_FLAG_END, meter, &rec);

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

/*
static midlts_err_t mid_session_parse_version(mid_session_version_fw_t *fw, const char *version) {
	uint16_t a, b, c, d;
	if (sscanf(version, "%hu.%hu.%hu.%hu", &a, &b, &c, &d) == 4) {
		if (a < 1024 && b < 1024 && c < 1024 && d < 1024) {
			fw->major = a;
			fw->minor = b;
			fw->patch = c;
			fw->extra = d;
			return LTS_OK;
		}
	}
	return LTS_PARSE_VERSION;
}
*/

static midlts_err_t mid_session_init_partition(midlts_ctx_t *ctx, const esp_partition_t *partition, size_t flash_size, time_t now,
		mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version) {
	midlts_err_t ret = LTS_OK;

	if (flash_size % FLASH_PAGE_SIZE != 0) {
		return LTS_READ;
	}

	if (flash_size / FLASH_PAGE_SIZE > sizeof (ctx->status)) {
		return LTS_READ;
	}

	uint16_t minp = 0xFFFF;
	uint16_t maxp = 0xFFFF;
	midlts_id_t minid = 0;
	midlts_id_t maxid = 0;

	memset(ctx, 0, sizeof (*ctx));

	ctx->partition = partition;
	ctx->lr_version = lr_version;
	ctx->fw_version = fw_version;

	static uint8_t page[FLASH_PAGE_SIZE];
	static uint8_t raw[FLASH_PAGE_SIZE];

	size_t npages = partition->size / FLASH_PAGE_SIZE;

	// First find status of pages
	char desc[sizeof (ctx->status)+1] = {0};
	bool allempty = true;

	for (size_t i = 0; i < npages; i++) {
		esp_err_t err = esp_partition_read_raw(ctx->partition, i * FLASH_PAGE_SIZE, raw, sizeof (raw));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		bool empty = true;
		for (size_t j = 0; j < sizeof (raw); j++) {
			if (raw[j] != 0xff) {
				empty = false;
				break;
			}
		}

		if (empty) {
			desc[i] = 'E';
			ctx->status[i] = MIDLTS_PAGE_EMPTY;
			continue;
		}

		allempty = false;

		// Not empty, check if valid or not (no raw, want decryption if enabled!)
		err = esp_partition_read(ctx->partition, i * FLASH_PAGE_SIZE, page, sizeof (page));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		size_t j = 0;
		for (j = 0; j < sizeof (page); j += sizeof (mid_session_record_t)) {
			mid_session_record_t rec = *(mid_session_record_t *)&page[j];

			if (!mid_session_check_crc(&rec)) {
				break;
			}

			if (minp == 0xFFFF) {
				minp = maxp = i;
				minid = maxid = rec.rec_id;
			}

			if (rec.rec_id > maxid) {
				maxid = rec.rec_id;
				maxp = i;
			}

			if (rec.rec_id < minid) {
				minid = rec.rec_id;
				minp = i;
			}
		}

		bool valid = true;
		for (size_t k = j; k < sizeof (page); k++) {
			if (raw[k] != 0xFF) {
				valid = false;
				break;
			}
		}

		if (valid) {
			desc[i] = 'V';
			ctx->status[i] = MIDLTS_PAGE_VALID;
		} else {
			desc[i] = 'I';
			ctx->status[i] = MIDLTS_PAGE_INVALID;
		}
	}

	ESP_LOGI(TAG, "MID Session Recovery - %s", desc);

	if (minp != 0xFFFF) {
		ESP_LOGI(TAG, "MID Session Recovery - Min %u/%u Max %u/%u", minp, minid, maxp, maxid);
	}

	if (allempty) {
		return LTS_OK;
	}

	// Check continuity of records from head to tail
	midlts_id_t curpage = minp;
	midlts_id_t id = 0;
	midlts_id_t offset = 0;

	while (true) {
		esp_err_t err = esp_partition_read(ctx->partition, curpage * FLASH_PAGE_SIZE, page, sizeof (page));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		err = esp_partition_read_raw(ctx->partition, curpage * FLASH_PAGE_SIZE, raw, sizeof (raw));
		if (err != LTS_OK) {
			return LTS_READ;
		}

		for (offset = 0; offset < sizeof (page); offset += sizeof (mid_session_record_t)) {
			mid_session_record_t rec = *(mid_session_record_t *)&page[offset];
			if (!mid_session_check_crc(&rec)) {
				break;
			}

			mid_session_print_record(&rec);

			if (curpage == minp && offset == 0) {
				id = rec.rec_id;
			} else if (rec.rec_id != (id + 1)) {
				return LTS_MSG_OUT_OF_ORDER;
			} else {
				id++;
			}
		}

		if (curpage == maxp) {
			break;
		}

		curpage = (curpage + 1) % npages;
	}

	ESP_LOGI(TAG, "MID Session Recovery - Current %" PRIu32 " / Page %" PRIu32 " / Offset %" PRIu32, id, curpage, offset);

	ctx->msg_addr = curpage * FLASH_PAGE_SIZE + offset;
	ctx->msg_id = id;

	return ret;
}

midlts_err_t mid_session_init(midlts_ctx_t *ctx, time_t now, mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version) {
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
		return LTS_ERASE;
	}
	return LTS_OK;
}

midlts_err_t mid_session_reset_page(size_t addr) {
	const esp_partition_t *partition;
	if ((partition = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "mid")) == NULL) {
		return LTS_READ;
	}
	esp_err_t err = esp_partition_erase_range(partition, addr * 4096, 4096);
	if (err != ESP_OK) {
		return LTS_ERASE;
	}
	return LTS_OK;
}

midlts_err_t mid_session_read_record(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec) {
	if (pos->loc > ctx->partition->size) {
		return LTS_READ;
	}

	uint8_t data[sizeof (mid_session_record_t)];

	if (esp_partition_read(ctx->partition, pos->loc, data, sizeof (data)) != ESP_OK) {
		return LTS_READ;
	}

	*rec = *(mid_session_record_t *)data;

	uint32_t crc = rec->rec_crc;
	rec->rec_crc = 0xFFFFFFFF;
	uint32_t crc2 = esp_crc32_le(0, (uint8_t *)rec, sizeof (*rec));
	rec->rec_crc = crc;

	if (crc != crc2) {
		return LTS_BAD_CRC;
	}

	if (rec->rec_id != pos->id) {
		return LTS_BAD_ARG;
	}

	return LTS_OK;
}
