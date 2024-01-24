#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "crc16.h"
#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"

#include "mid_event.h"
#include "mid_status.h"
#include "mid.h"

static const char *TAG = "MIDEVENT       ";

static bool mid_get_event_log_entry(uint32_t index, uint16_t *buf, size_t bufsize) {
	ZapMessage msg = MCU_SendUint32WithReply(ParamMidEventLog, index);
	if (bufsize != 14 || msg.length != 14 || msg.type != MsgWriteAck || msg.identifier != ParamMidEventLog) {
		return false;
	}
	memcpy(buf, msg.data, msg.length);
	return true;
}

bool mid_get_event_log(mid_event_log_t *log) {
	//ESP_LOGI(TAG, "MID Event Log: Reading ...");

	int timeout = 1000;
	int remaining = 1;
	int index = 0;

	uint16_t buf[7];

	while (remaining > 0 && timeout-- > 0) {
		//ESP_LOGI(TAG, "MID Event Log: Requesting %d ...", index);

		if (!mid_get_event_log_entry(index, buf, sizeof (buf))) {
			ESP_LOGE(TAG, "MID Event Log: Failed to read entry");
			return false;
		}

		uint16_t i = buf[0];
		if (i != index) {
			ESP_LOGE(TAG, "MID Event Log: Expected index %d but got %d!", index, i);
			return false;
		}

		uint16_t rem = buf[1];
		remaining = rem;

		uint16_t erase = buf[2];
		uint16_t head = buf[3];
		uint16_t data = buf[4];
		// Unused?
		// uint16_t ent = buf[5];
		uint16_t crc = buf[6];

		uint16_t crc2 = crc16(0xA1FE, (uint8_t *)buf, sizeof (buf) - 2);

		if (crc != crc2) {
			ESP_LOGE(TAG, "MID Event Log: Checksums do not match %04X != %04X!", crc, crc2);
		}

		uint16_t seq = head & 0x1FFF;
		mid_event_log_type_t type = head >> 13;

		const char *typestr = NULL;

		switch (type) {
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
				return false;
		}

		mid_event_log_entry_t entry = {
			.type = type,
			.seq = seq,
			.data = data,
		};

		uint8_t app = (data >> 8) & 0xFF;
		uint8_t bl = (data & 0xFF) & 0x1F;

		switch (type) {
			case MID_EVENT_LOG_TYPE_INIT:
			case MID_EVENT_LOG_TYPE_ERASE:
				ESP_LOGI(TAG, "MID Event Log: %03d (%03d Remain, Erase %d - %04X): %3d [%7s] x %d", index, remaining, erase, crc, seq, typestr, data);
				break;
			case MID_EVENT_LOG_TYPE_START:
			case MID_EVENT_LOG_TYPE_SUCCESS:
			case MID_EVENT_LOG_TYPE_FAIL:
				ESP_LOGI(TAG, "MID Event Log: %03d (%03d Remain, Erase %d - %04X): %3d [%7s] v1.%d.%d", index, remaining, erase, crc, seq, typestr, app, bl);
				break;
			default:
				return false;
		}

		if (mid_event_log_add(log, &entry) < 0) {
			return false;
		}

		index++;
	}

	return true;
}
