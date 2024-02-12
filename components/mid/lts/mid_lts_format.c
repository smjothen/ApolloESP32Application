#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#include "utz.h"

#include "mid_session.h"
#include "mid_lts.h"
#include "mid_lts_priv.h"

static const char *TAG = "MIDLTS         ";

const char *mid_session_get_auth_type_name(mid_session_auth_type_t type) {
	switch(type) {
		case MID_SESSION_AUTH_TYPE_RFID:
			return "UID";
		case MID_SESSION_AUTH_TYPE_UUID:
			return "UUID";
		case MID_SESSION_AUTH_TYPE_EMAID:
			return "EMAID";
		case MID_SESSION_AUTH_TYPE_EVCCID:
			return "EVCCID";
		case MID_SESSION_AUTH_TYPE_STRING:
			return "String";
		case MID_SESSION_AUTH_TYPE_UNKNOWN:
			return "Unknown";
	}
	return "Unknown";
}

const char *mid_session_get_auth_source_name(mid_session_auth_source_t source) {
	switch(source) {
		case MID_SESSION_AUTH_SOURCE_RFID:
			return "RFID";
		case MID_SESSION_AUTH_SOURCE_BLE:
			return "BLE";
		case MID_SESSION_AUTH_SOURCE_CLOUD:
			return "Cloud";
		case MID_SESSION_AUTH_SOURCE_ISO15118:
			return "ISO15118";
		case MID_SESSION_AUTH_SOURCE_UNKNOWN:
			return "Unknown";
	}
	return "Unknown";
}


void mid_session_format_bytes_uuid(char *buf, uint8_t *bytes, size_t len) {
	char *ptr = buf;
	for (int i = 0; i < len; i++) {
		bool inter = i == 3 || i == 5 || i == 7 || i == 9;
		ptr += sprintf(ptr, "%02x%s", bytes[i], inter ? "-" : "");
	}
}

void mid_session_format_bytes(char *buf, uint8_t *bytes, size_t len) {
	char *ptr = buf;
	for (int i = 0; i < len; i++) {
		ptr += sprintf(ptr, "%02x", bytes[i]);
	}
}

void mid_session_format_record_id(mid_session_id_t *id, char *buf, size_t buf_size) {
	char uuid_buf[64] = {0};
	mid_session_format_bytes_uuid(uuid_buf, id->uuid, sizeof (id->uuid));
	snprintf(buf, buf_size, "%s", uuid_buf);
}

void mid_session_format_record_auth(mid_session_auth_t *auth, char *buf, size_t buf_size) {
	char rfid_buf[64] = {0};
	mid_session_format_bytes(rfid_buf, auth->tag, auth->length);
	const char *type = mid_session_get_auth_type_name(auth->type);
	const char *source = mid_session_get_auth_source_name(auth->source);
	snprintf(buf, buf_size, "%s - %s %s", source, type, rfid_buf);
}

void mid_session_format_record_meter_value(mid_session_meter_value_t *mv, char *buf, size_t buf_size) {
	char type[16] = {0};
	if (mv->flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
		strlcat(type, "S", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
		strlcat(type, "E", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF) {
		strlcat(type, "T", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN) {
		strlcat(type, "U", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE) {
		strlcat(type, "I", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED) {
		strlcat(type, "Y", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE) {
		strlcat(type, "R", sizeof (type));
	}
	if (mv->flag & MID_SESSION_METER_VALUE_FLAG_METER_ERROR) {
		strlcat(type, "M", sizeof (type));
	}

	char buf2[64];

	udatetime_t dt;
	utz_datetime_init_timespec(&dt, &MID_TIME_TO_TS(mv->time));
	utz_datetime_format_iso_ocmf(buf2, sizeof (buf2), &dt);

	snprintf(buf, buf_size, "FW %d.%d.%d.%d / LR %d.%d.%d - %s / %08" PRId32 " - %s",
			mv->fw.major, mv->fw.minor, mv->fw.patch, mv->fw.extra,
			mv->lr.major, mv->lr.minor, mv->lr.patch,
			buf2, mv->meter,
			type);
}

void mid_session_print_record_pos(midlts_pos_t *pos, mid_session_record_t *rec) {
	char buf[128];

	char posbuf[16] = {0};
	if (pos) {
		snprintf(posbuf, sizeof (posbuf), "%04X:%04X", pos->id, pos->offset);
	}

	switch(rec->rec_type) {
		case MID_SESSION_RECORD_TYPE_ID: {
			mid_session_format_record_id(&rec->id, buf, sizeof (buf));
			ESP_LOGI(TAG, "MID Session Record %s - SessionId  - #%08" PRIu32 " - CRC %08" PRIx32 " - %s", posbuf, rec->rec_id, rec->rec_crc, buf);
			break;
		}
		case MID_SESSION_RECORD_TYPE_AUTH: {
			mid_session_format_record_auth(&rec->auth, buf, sizeof (buf));
			ESP_LOGI(TAG, "MID Session Record %s - Auth       - #%08" PRIu32 " - CRC %08" PRIx32 " - %s", posbuf, rec->rec_id, rec->rec_crc, buf);
			break;
		}
		case MID_SESSION_RECORD_TYPE_METER_VALUE: {
			mid_session_format_record_meter_value(&rec->meter_value, buf, sizeof (buf));
			ESP_LOGI(TAG, "MID Session Record %s - MeterValue - #%08" PRIu32 " - CRC %08" PRIx32 " - %s", posbuf, rec->rec_id, rec->rec_crc, buf);
			break;
		}
	}
}

void mid_session_print_record(mid_session_record_t *rec) {
	mid_session_print_record_pos(NULL, rec);
}
