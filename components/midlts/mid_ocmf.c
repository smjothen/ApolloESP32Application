#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "mid_event.h"
#include "mid_ocmf.h"

static const char *TAG = "MIDOCMF        ";

static int midocmf_format_time(char *buf, size_t size, mid_session_meter_value_t *value) {
	udatetime_t dt;
	utz_unix_to_datetime(MID_TIME_UNPACK(value->time), &dt);
	utz_datetime_format_iso_ocmf(buf, size, &dt);
	return 0;
}

static int midocmf_format_fw_version(char *buf, size_t size, mid_session_meter_value_t *value) {
	mid_session_version_fw_t fw = value->fw;
	snprintf(buf, size, "%d.%d.%d.%d", fw.major, fw.minor, fw.patch, fw.extra);
	return 0;
}

static int midocmf_format_lr_version(char *buf, size_t size, mid_session_meter_value_t *value) {
	mid_session_version_lr_t lr = value->lr;
	snprintf(buf, size, "v%d.%d.%d", lr.major, lr.minor, lr.patch);
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

int midocmf_fiscal_from_meter_value(char *outbuf, size_t size, const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log) {
	if (!value) {
		return -1;
	}

	if (!(value->flag & MID_SESSION_METER_VALUE_READING_FLAG_TARIFF)) {
		// Only support serializing tariff change values?
		ESP_LOGE(TAG, "Attempt to serialize non-tariff change as fiscal message!");
		return -1;
	}

	cJSON *obj = cJSON_CreateObject();
	if (!obj) {
		ESP_LOGI(TAG, "Couldn't allocate JSON struct!");
		return -1;
	}

	char buf[64];

	cJSON_AddStringToObject(obj, "FV", "1.0");
	cJSON_AddStringToObject(obj, "GI", "Zaptec Go Plus");
	cJSON_AddStringToObject(obj, "GS", serial);

	midocmf_format_fw_version(buf, sizeof (buf), value);
	cJSON_AddStringToObject(obj, "GV", buf);

	midocmf_format_lr_version(buf, sizeof (buf), value);
	cJSON_AddStringToObject(obj, "MF", buf);

	cJSON_AddStringToObject(obj, "PG", "F1");

	cJSON *readerArray = cJSON_CreateArray();
	cJSON *readerObject = cJSON_CreateObject();


	midocmf_format_time(buf, sizeof (buf), value);

	const char *time_state = " U";
	if (value->flag & MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED) {
		time_state = " S";
	} else if (value->flag & MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE) {
		time_state = " I";
	} else if (value->flag & MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE) {
		time_state = " R";
	}

	strlcat(buf, time_state, sizeof (buf));

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
		return -1;
	}

	char *json = cJSON_PrintUnformatted(obj);
	snprintf(outbuf, size, "OCMF|%s", json);

	cJSON_Delete(obj);
	free(json);

	return 0;
}

int midocmf_fiscal_from_record(char *outbuf, size_t size, const char *serial, mid_session_record_t *value, mid_event_log_t *log) {
	if (!value || value->rec_type != MID_SESSION_RECORD_TYPE_METER_VALUE) {
		return -1;
	}
	return midocmf_fiscal_from_meter_value(outbuf, size, serial, &value->meter_value, log);
}
