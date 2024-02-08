#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "mid_lts.h"
#include "mid_sign.h"
#include "mid_event.h"
#include "mid_ocmf.h"

#define MID_OCMF_FORMAT_VERSION_FV "1.0"
#define MID_OCMF_GATEWAY_IDENTIFICATION_GI "Zaptec Go+"

static const char *TAG = "MIDOCMF        ";

static int midocmf_format_time(char *buf, size_t size, mid_session_meter_value_t *value) {
	udatetime_t dt;
	utz_datetime_init_timespec(&dt, &MID_TIME_TO_TS(value->time));
	utz_datetime_format_iso_ocmf(buf, size, &dt);
	return 0;
}

static const char *midocmf_get_time_status_from_flag(uint32_t flag) {
	const char *time_status = NULL;

	if (flag & MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED) {
		time_status = " S";
	} else if (flag & MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE) {
		time_status = " I";
	} else if (flag & MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE) {
		time_status = " R";
	} else if (flag & MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN) {
		time_status = " U";
	}

	return time_status;
}

static const char *midocmf_get_transaction_type_from_flag(uint32_t flag) {
	const char *tx = NULL;

	if (flag & MID_SESSION_METER_VALUE_READING_FLAG_START) {
		tx = "B";
	} else if (flag & MID_SESSION_METER_VALUE_READING_FLAG_END) {
		tx = "E";
	} else if (flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF) {
		tx = "T";
	}

	return tx;
}

static int midocmf_format_fw_version(char *buf, size_t size, mid_session_version_fw_t *fw) {
	snprintf(buf, size, "%d.%d.%d.%d", fw->major, fw->minor, fw->patch, fw->extra);
	return 0;
}

static int midocmf_format_lr_version(char *buf, size_t size, mid_session_version_lr_t *lr) {
	snprintf(buf, size, "v%d.%d.%d", lr->major, lr->minor, lr->patch);
	return 0;
}

static int midocmf_format_uuid_bytes(char *buf, size_t size, uint8_t *bytes) {
	if (size < 36 + 1) {
		return -1;
	}

	char *ptr = buf;
	for (size_t i = 0; i < 16; i++) {
		bool inter = i == 3 || i == 5 || i == 7 || i == 9;
		ptr += sprintf(ptr, "%02x%s", bytes[i], inter ? "-" : "");
	}

	return 0;
}

static int midocmf_format_uuid(char *buf, size_t size, mid_session_id_t *id) {
	return midocmf_format_uuid_bytes(buf, size, id->uuid);
}

static int midocmf_format_raw_bytes(char *buf, size_t bufsize, uint8_t *bytes, size_t len) {
	if (bufsize < len * 2 + 1) {
		return -1;
	}

	char *ptr = buf;
	for (uint8_t i = 0; i < len; i++) {
		ptr += sprintf(ptr, "%02X", bytes[i]);
	}

	return 0;
}

static int midocmf_format_auth(char *buf, size_t bufsize, mid_session_auth_t *auth) {
	size_t max_tag = sizeof (auth->tag);
	size_t length = auth->length > max_tag ? max_tag : auth->length;

	switch(auth->type) {
		case MID_SESSION_AUTH_TYPE_RFID:
		case MID_SESSION_AUTH_TYPE_EMAID:
		case MID_SESSION_AUTH_TYPE_EVCCID:
			midocmf_format_raw_bytes(buf, bufsize, auth->tag, length);
			break;
		case MID_SESSION_AUTH_TYPE_UUID:
			midocmf_format_uuid_bytes(buf, bufsize, auth->tag);
			break;
		case MID_SESSION_AUTH_TYPE_STRING:
			snprintf(buf, bufsize, "%.*s", length, auth->tag);
			break;
		case MID_SESSION_AUTH_TYPE_UNKNOWN:
		default:
			buf[0] = 0;
			break;
	}

	return 0;
}

static int midocmf_fiscal_add_event_log(cJSON *json, mid_event_log_t *log) {
	if (!log) {
		// No log requested, no problem
		return 0;
	}

	cJSON *eventArray = cJSON_CreateArray();
	if (!eventArray) {
		return -1;
	}

	for (int i = 0; i < log->count; i++) {
		mid_event_log_entry_t *entry = &log->entries[i];

		const char *typestr = NULL;
		switch (entry->type) {
			case MID_EVENT_LOG_TYPE_INIT:
				typestr = "INIT";
				break;
			case MID_EVENT_LOG_TYPE_ERASE:
				typestr = "ERASE";
				break;
			case MID_EVENT_LOG_TYPE_START:
				typestr = "START";
				break;
			case MID_EVENT_LOG_TYPE_SUCCESS:
				typestr = "SUCCESS";
				break;
			case MID_EVENT_LOG_TYPE_FAIL:
				typestr = "FAIL";
				break;
			default:
				break;
		}

		if (!typestr) {
			continue;
		}

		cJSON *eventObj = cJSON_CreateObject();
		if (!eventObj) {
			cJSON_Delete(eventArray);
			return -1;
		}

		cJSON_AddNumberToObject(eventObj, "ES", entry->seq);
		cJSON_AddStringToObject(eventObj, "ET", typestr);

		char buf[64];

		uint8_t app = (entry->data >> 8) & 0xFF;
		uint8_t bl = (entry->data & 0xFF) & 0x1F;

		switch (entry->type) {
			case MID_EVENT_LOG_TYPE_INIT:
			case MID_EVENT_LOG_TYPE_ERASE:
				cJSON_AddNumberToObject(eventObj, "EC", entry->data);
				break;
			case MID_EVENT_LOG_TYPE_START:
			case MID_EVENT_LOG_TYPE_SUCCESS:
			case MID_EVENT_LOG_TYPE_FAIL:
				snprintf(buf, sizeof (buf), "v1.%d.%d", app, bl);
				cJSON_AddStringToObject(eventObj, "EV", buf);
				break;
			default:
				break;
		}

		cJSON_AddItemToArray(eventArray, eventObj);
	}

	cJSON_AddItemToObject(json, "ZE", eventArray);

	return 0;
}

static const char *midocmf_fiscal_from_meter_value_payload(const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log) {
	if (!value) {
		return NULL;
	}

	if (!(value->flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF)) {
		// Only support serializing tariff change values?
		ESP_LOGE(TAG, "Attempt to serialize non-tariff change as fiscal message!");
		return NULL;
	}

	cJSON *obj = cJSON_CreateObject();
	if (!obj) {
		ESP_LOGI(TAG, "Couldn't allocate JSON struct!");
		return NULL;
	}

	char buf[64];

	cJSON_AddStringToObject(obj, "FV", MID_OCMF_FORMAT_VERSION_FV);
	cJSON_AddStringToObject(obj, "GI", MID_OCMF_GATEWAY_IDENTIFICATION_GI);
	cJSON_AddStringToObject(obj, "GS", serial);

	midocmf_format_fw_version(buf, sizeof (buf), &value->fw);
	cJSON_AddStringToObject(obj, "GV", buf);

	midocmf_format_lr_version(buf, sizeof (buf), &value->lr);
	cJSON_AddStringToObject(obj, "MF", buf);

	cJSON_AddStringToObject(obj, "PG", "F1");

	cJSON *readerArray = cJSON_CreateArray();
	cJSON *readerObject = cJSON_CreateObject();

	midocmf_format_time(buf, sizeof (buf), value);

	const char *time_status = midocmf_get_time_status_from_flag(value->flag);
	if (!time_status) {
		cJSON_Delete(readerArray);
		cJSON_Delete(readerObject);
		cJSON_Delete(obj);
		return NULL;
	}

	strlcat(buf, time_status, sizeof (buf));

	cJSON_AddStringToObject(readerObject, "TM", buf);

	cJSON_AddNumberToObject(readerObject, "RV", value->meter / 1000.0);
	cJSON_AddStringToObject(readerObject, "RI", "1-0:1.8.0");
	cJSON_AddStringToObject(readerObject, "RU", "kWh");
	cJSON_AddStringToObject(readerObject, "RT", "AC");

	// TODO: Do we send entries with meter errors to the cloud?
	cJSON_AddStringToObject(readerObject, "ST", "G");

	cJSON_AddItemToArray(readerArray, readerObject);
	cJSON_AddItemToObject(obj, "RD", readerArray);

	if (midocmf_fiscal_add_event_log(obj, log) < 0) {
		ESP_LOGE(TAG, "Error appending event log to entry!");
		cJSON_Delete(obj);
		return NULL;
	}

	char *json = cJSON_PrintUnformatted(obj);

	cJSON_Delete(obj);
	return json;
}

static const char *midocmf_sign_data(mid_sign_ctx_t *ctx, const char *data) {
	if (!mid_sign_ctx_ready(ctx)) {
		ESP_LOGE(TAG, "Not ready to sign OCMF packages!");
		return NULL;
	}

	cJSON *sigObj = cJSON_CreateObject();
	if (!sigObj) {
		ESP_LOGE(TAG, "Error allocating JSON object");
		return NULL;
	}

	static char sig_buf[256];
	size_t sig_len = sizeof (sig_buf);

	if (mid_sign_ctx_sign(ctx, (char *)data, strlen(data), sig_buf, &sig_len) != 0) {
		cJSON_Delete(sigObj);
		ESP_LOGE(TAG, "Error signing fiscal message!");
		return NULL;
	}

	cJSON_AddStringToObject(sigObj, "SA", "ECDSA-secp384r1-SHA256");
	cJSON_AddStringToObject(sigObj, "SE", "base64");
	cJSON_AddStringToObject(sigObj, "SD", sig_buf);

	char *json = cJSON_PrintUnformatted(sigObj);
	if (!json) {
		cJSON_Delete(sigObj);
		ESP_LOGE(TAG, "Error allocating JSON serialization!");
		return NULL;
	}

	cJSON_Delete(sigObj);
	return json;
}

static const char *midocmf_signed_ocmf_payload(mid_sign_ctx_t *ctx, const char *payload) {
	const char *signature = NULL;

	if (ctx && (signature = midocmf_sign_data(ctx, payload)) == NULL) {
		ESP_LOGE(TAG, "Couldn't sign OCMF package!");
		free((char *)payload);
		return NULL;
	}

	size_t buf_size = 5 + strlen(payload) + (signature ? 1 + strlen(signature) : 0) + 1;
	char *buf =  malloc(buf_size);
	snprintf(buf, buf_size, "OCMF|%s%s%s", payload, signature ? "|" : "", signature ? signature : "");

	free((char *)payload);

	if (signature) {
		free((char *)signature);
	}
	return buf;
}

const char *midocmf_signed_fiscal_from_meter_value(mid_sign_ctx_t *ctx, const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log) {
	const char *payload = NULL;
	if ((payload = midocmf_fiscal_from_meter_value_payload(serial, value, log)) == NULL) {
		return NULL;
	}

	const char *ocmf_signed = midocmf_signed_ocmf_payload(ctx, payload);
	return ocmf_signed;
}

const char *midocmf_signed_fiscal_from_record(mid_sign_ctx_t *ctx, const char *serial, mid_session_record_t *value, mid_event_log_t *log) {
	if (!value || value->rec_type != MID_SESSION_RECORD_TYPE_METER_VALUE) {
		return NULL;
	}
	return midocmf_signed_fiscal_from_meter_value(ctx, serial, &value->meter_value, log);
}

static const char *midocmf_transaction_from_active_session(mid_sign_ctx_t *ctx, const char *serial, midlts_active_t *active_session) {
	if (active_session->count <= 0) {
		ESP_LOGE(TAG, "Can't serialize empty session!");
		return NULL;
	}

	cJSON *obj = cJSON_CreateObject();
	if (!obj) {
		ESP_LOGE(TAG, "Couldn't allocate JSON struct!");
		return NULL;
	}

	char buf[64];

	cJSON_AddStringToObject(obj, "FV", MID_OCMF_FORMAT_VERSION_FV);
	cJSON_AddStringToObject(obj, "GI", MID_OCMF_GATEWAY_IDENTIFICATION_GI);
	cJSON_AddStringToObject(obj, "GS", serial);

	midocmf_format_fw_version(buf, sizeof (buf), &active_session->fw);
	cJSON_AddStringToObject(obj, "GV", buf);

	midocmf_format_lr_version(buf, sizeof (buf), &active_session->lr);
	cJSON_AddStringToObject(obj, "MF", buf);

	cJSON_AddStringToObject(obj, "PG", "T1");

	if (active_session->has_auth) {
		mid_session_auth_t *auth = &active_session->auth;

		cJSON_AddBoolToObject(obj, "IS", true);

		switch (auth->source) {
			case MID_SESSION_AUTH_SOURCE_BLE:
			case MID_SESSION_AUTH_SOURCE_RFID:
				cJSON_AddStringToObject(obj, "IL", "HEARSAY");
				break;
			case MID_SESSION_AUTH_SOURCE_CLOUD:
				cJSON_AddStringToObject(obj, "IL", "TRUSTED");
				break;
			case MID_SESSION_AUTH_SOURCE_ISO15118:
				// TODO: Is this always secure?
				cJSON_AddStringToObject(obj, "IL", "SECURE");
				break;
			case MID_SESSION_AUTH_SOURCE_UNKNOWN:
			default:
				cJSON_AddStringToObject(obj, "IL", "NONE");
				break;
		}

		if (auth->source == MID_SESSION_AUTH_SOURCE_RFID) {
			cJSON *flagArray = cJSON_CreateArray();
			cJSON_AddItemToArray(flagArray, cJSON_CreateString("RFID_RELATED"));
			cJSON_AddItemToObject(obj, "IF", flagArray);
		}

#define ISO15693_LENGTH 8

		switch(auth->type) {
			case MID_SESSION_AUTH_TYPE_RFID:
				if (auth->length == ISO15693_LENGTH) {
					cJSON_AddStringToObject(obj, "IT", "ISO15693");
				} else {
					cJSON_AddStringToObject(obj, "IT", "ISO14443");
				}
				break;
			case MID_SESSION_AUTH_TYPE_UUID:
				cJSON_AddStringToObject(obj, "IT", "CENTRAL");
				break;
			case MID_SESSION_AUTH_TYPE_STRING:
				// TODO: What to use for generic "string" type?
				cJSON_AddStringToObject(obj, "IT", "UNDEFINED");
				break;
			case MID_SESSION_AUTH_TYPE_EMAID:
				cJSON_AddStringToObject(obj, "IT", "EMAID");
				break;
			case MID_SESSION_AUTH_TYPE_EVCCID:
				cJSON_AddStringToObject(obj, "IT", "EVCCID");
				break;
			case MID_SESSION_AUTH_TYPE_UNKNOWN:
			default:
				cJSON_AddStringToObject(obj, "IT", "NONE");
				break;
		}

		char data[64];
		midocmf_format_auth(data, sizeof (data), auth);
		cJSON_AddStringToObject(obj, "ID", data);
	} else {
		cJSON_AddBoolToObject(obj, "IS", false);
	}

	cJSON *readerArray = cJSON_CreateArray();

	for (size_t i = 0; i < active_session->count; i++) {
		mid_session_meter_value_t *reading = &active_session->events[i];

		cJSON *readerObject = cJSON_CreateObject();

		const char *obis = "1-0:1.8.0";
		const char *tx_type = midocmf_get_transaction_type_from_flag(reading->flag);
		const char *time_status = midocmf_get_time_status_from_flag(reading->flag);

		if (!tx_type || !time_status) {
			ESP_LOGE(TAG, "No valid reading flag: %08" PRIX16, reading->flag);
			cJSON_Delete(readerObject);
			cJSON_Delete(readerArray);
			cJSON_Delete(obj);
			return NULL;
		}

		midocmf_format_time(buf, sizeof (buf), reading);
		strlcat(buf, time_status, sizeof (buf));

		cJSON_AddStringToObject(readerObject, "TM", buf);
		cJSON_AddStringToObject(readerObject, "TX", tx_type);

		if (i == 0) {
			cJSON_AddStringToObject(readerObject, "RU", "kWh");
		}

		if (reading->flag & MID_SESSION_METER_VALUE_FLAG_METER_ERROR) {
			const char *meter_state = "E";
			cJSON_AddStringToObject(readerObject, "ST", meter_state);
		} else {
			const char *meter_state = "G";
			cJSON_AddStringToObject(readerObject, "RI", obis);
			cJSON_AddNumberToObject(readerObject, "RV", reading->meter / 1000.0);
			cJSON_AddStringToObject(readerObject, "ST", meter_state);
		}

		cJSON_AddItemToArray(readerArray, readerObject);
	}

	cJSON_AddItemToObject(obj, "RD", readerArray);

	if (active_session->has_id) {
		midocmf_format_uuid(buf, sizeof (buf), &active_session->id);
		cJSON_AddStringToObject(obj, "ZS", buf);
	}

	char *json = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);

	return json;
}

const char *midocmf_signed_transaction_from_active_session(mid_sign_ctx_t *ctx, const char *serial, midlts_active_t *active_session) {
	const char *payload = NULL;
	if ((payload = midocmf_transaction_from_active_session(ctx, serial, active_session)) == NULL) {
		ESP_LOGE(TAG, "Error creating OCMF transaction");
		return NULL;
	}

	const char *ocmf_signed = midocmf_signed_ocmf_payload(ctx, payload);
	if (!ocmf_signed) {
		ESP_LOGE(TAG, "Error signing OCMF transaction");
	}
	return ocmf_signed;
}
