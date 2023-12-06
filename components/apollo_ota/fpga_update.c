#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "pic_update.h"

#include "zaptec_protocol_serialisation.h"
#include "protocol_task.h"
#include "mcu_communication.h"
#include "crc32.h"

#define TAG "fpga_update"

extern const uint8_t _pic_fpga_start[] asm("_binary_cc_bin_start");
extern const uint8_t _pic_fpga_end[] asm("_binary_cc_bin_end");

static ZapMessage txMsg;

static uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
static uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];

enum ZEFPGAType {
	MAX10 = 0,
	iCE40 = 1,
};

bool fpga_get_type(enum ZEFPGAType *type) {
	txMsg.type = MsgRead;
	txMsg.identifier = ParamFpgaType;

    uint encoded_length = ZEncodeMessageHeaderOnly(
        &txMsg, txBuf, encodedTxBuf
    );

    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

	bool success = false;
    if (rxMsg.type == MsgReadAck && rxMsg.length == 1){
		success = true;
		*type = rxMsg.data[0];
	} else {
		ESP_LOGE(TAG, "Couldn't read FPGA type!");
	}

	freeZapMessageReply();

	return success;
}

bool fpga_needs_configuration(bool *needsConfig) {
	txMsg.type = MsgRead;
	txMsg.identifier = ParamFpgaNeedsConfiguration;

    uint encoded_length = ZEncodeMessageHeaderOnly(
        &txMsg, txBuf, encodedTxBuf
    );

    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

	bool success = false;
    if (rxMsg.type == MsgReadAck && rxMsg.length == 1){
		success = true;
		*needsConfig = rxMsg.data[0];
	} else {
		ESP_LOGE(TAG, "Couldn't read if FPGA needs configuration!");
	}

	freeZapMessageReply();

	return success;
}

bool fpga_ensure_configured(void) {
	enum ZEFPGAType type = MAX10;
	if (!fpga_get_type(&type)) {
		return false;
	}

	if (type != iCE40) {
		return true;
	}

	if (type == iCE40) {
		bool needsConfig = false;
		if (!fpga_needs_configuration(&needsConfig)) {
			return false;
		}

		if (!needsConfig) {
			return true;
		}

		ESP_LOGI(TAG, "FPGA bitstream update!");

		uint16_t counter = 0;
		uint8_t buf[114];

		uint8_t *ptr = (uint8_t *)_pic_fpga_start;

		while (ptr < _pic_fpga_end) {
			size_t bytes_left = 111;
			if (_pic_fpga_end - ptr < bytes_left) {
				bytes_left = _pic_fpga_end - ptr;
			}

			ZEncodeUint16(counter, &buf[0]);
			ZEncodeUint8(bytes_left, &buf[2]);
			memcpy(buf + 3, ptr, bytes_left);

			txMsg.type = MsgCommand;
			txMsg.identifier = CommandFpgaBitstreamData;

			uint encoded_length = ZEncodeMessageHeaderAndByteArrayNoCheck(
				&txMsg, (char *) buf, 3 + bytes_left, txBuf, encodedTxBuf
			);

			ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

			if (rxMsg.type == MsgCommandAck) {
				ESP_LOGI(TAG, "FPGA programming chunk %d (%dbytes) - %d / %d", counter, bytes_left, ptr - _pic_fpga_start, _pic_fpga_end - _pic_fpga_start);
			} else {
				ESP_LOGE(TAG, "FPGA programming chunk %d (%dbytes) failed! - %d / %d", counter, bytes_left, ptr - _pic_fpga_start, _pic_fpga_end - _pic_fpga_start);
				freeZapMessageReply();
				return false;
			}

			freeZapMessageReply();

			ptr += bytes_left;
			counter++;
		}

		ESP_LOGI(TAG, "FPGA programmed successfully!");
	}

	return true;
}
