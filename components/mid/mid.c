#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include "crc16.h"
#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"
#include "storage.h"
#include "esp_littlefs.h"

#include "mid_sign.h"
#include "mid_lts.h"
#include "mid_status.h"
#include "mid_private.h"
#include "mid.h"

static const char *TAG = "MID            ";

bool mid_get_package(mid_package_t *pkg) {
	ZapMessage msg = MCU_ReadParameter(SignedMeterValue);
	if (msg.length != sizeof (mid_package_t) || msg.identifier != SignedMeterValue) {
		return false;
	}

	*pkg = *(mid_package_t *)msg.data;
	return true;
}

bool mid_get_status(uint32_t *status) {
	mid_package_t pkg;
	if (mid_get_package(&pkg)) {
		*status = pkg.status;
		return true;
	}
	return false;
}

bool mid_get_watt_hours(uint32_t *watt_hours) {
	mid_package_t pkg;
	if (mid_get_package(&pkg)) {
		*watt_hours = pkg.watt_hours;
		return true;
	}
	return false;
}

bool mid_get_software_identifiers(uint8_t identifiers[3]) {
	mid_package_t pkg;
	if (mid_get_package(&pkg)) {
		identifiers[0] = pkg.identifiers[0];
		identifiers[1] = pkg.identifiers[1];
		identifiers[2] = pkg.identifiers[2];
		return true;
	}
	return false;
}

// TODO: These aren't legally relevant but are MID related, separate into non-LR module?
bool mid_get_calibration_id(uint32_t *id) {
	ZapMessage msg = MCU_ReadParameter(ParamMidStoredCalibrationId);
	if (msg.length != 4 || msg.identifier != ParamMidStoredCalibrationId) {
		return false;
	}
	*id = GetUint32_t(msg.data);
	return true;
}

bool mid_set_blink_enabled(bool enabled) {
	return MCU_SendUint8Parameter(ParamMIDBlinkEnabled, enabled) == MsgWriteAck;
}

bool mid_get_energy_interpolated(float *energy) {
	ZapMessage msg = MCU_ReadParameter(ParamSessionEnergyCountImportActiveInterpolated);
	if (msg.length == 4 && msg.type == MsgReadAck && msg.identifier == ParamSessionEnergyCountImportActiveInterpolated) {
		*energy = GetFloat(msg.data);
		return true;
	}
	return false;
}

bool mid_get_is_calibration_handle(void) {
	ZapMessage msg = MCU_ReadParameter(ParamIsCalibrationHandle);
	if (msg.length == 1 && msg.type == MsgReadAck && msg.identifier == ParamIsCalibrationHandle) {
		return msg.data[0];
	}
	return false;
}

static uint32_t mid_status = 0;
static midlts_ctx_t mid_lts = {0};

uint32_t mid_get_esp_status(void) {
	return mid_status;
}

int mid_init(const char *fw_version) {
	esp_vfs_littlefs_conf_t conf = {
		.base_path = "/mid",
		.partition_label = "mid",
		// Don't mount in case of failure, we will need to inform the user!
#ifdef MID_ALLOW_LITTLEFS_FORMAT
		.format_if_mount_failed = true,
#else
		.format_if_mount_failed = false,
#endif
		.dont_mount = false,
	};

	esp_err_t ret;
	if ((ret = esp_vfs_littlefs_register(&conf)) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount MID partition: %s!", esp_err_to_name(ret));
		mid_status |= MID_ESP_STATUS_FILESYSTEM;
		return -1;
	} else {
		mid_status &= ~MID_ESP_STATUS_FILESYSTEM;
	}

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();

	if (mid_sign_ctx_init(ctx, storage_Get_MIDPrivateKey(), storage_Get_MIDPublicKey()) != 0) {
		ESP_LOGE(TAG, "Public/private key failure!");

#ifdef MID_ALLOW_KEY_GENERATION
		ESP_LOGI(TAG, "Allowing generation of new public/private key pair!");

		if (mid_sign_ctx_generate(storage_Get_MIDPrivateKey(), MID_PRIVATE_KEY_SIZE, storage_Get_MIDPublicKey(), MID_PUBLIC_KEY_SIZE) != 0) {
			ESP_LOGE(TAG, "Failed to generate public/private key pair!");
			mid_status |= MID_ESP_STATUS_KEY;
			return -1;
		} else {
			storage_SaveConfiguration();
		}
#else
		mid_status |= MID_ESP_STATUS_KEY;
		return -1;
#endif
	} else {
		mid_status &= ~MID_ESP_STATUS_KEY;
	}

	char buffer[MID_PUBLIC_KEY_SIZE];
	if (storage_Get_MIDPublicKeyDER(buffer)) {
		ESP_LOGI(TAG, "MID public key: %s", buffer);
	}

	mid_event_log_t log;
	if (!mid_event_log_init(&log)) {
		if (mid_get_event_log(&log)) {
			// Function prints event log too for now, so do nothing
			mid_status &= ~MID_ESP_STATUS_EVENT_LOG;
		} else {
			ESP_LOGE(TAG, "Failure to read MID event log!");
			mid_status |= MID_ESP_STATUS_EVENT_LOG;
		}
		mid_event_log_free(&log);
	}

	// Get and parse current versions

	mid_session_version_fw_t fw_ver;

	int len;
	uint16_t a, b, c, d;
	if (sscanf(fw_version, "%" PRIu16 ".%" PRIu16 ".%" PRIu16 ".%" PRIu16 "%n", &a, &b, &c, &d, &len) == 4 &&
			strlen(fw_version) == len && a < 1024 && b < 1024 && c < 1024 && d < 1024) {
		fw_ver = (mid_session_version_fw_t) { a, b, c, d };
		mid_status &= ~MID_ESP_STATUS_INVALID_FW_VERSION;
	} else {
		mid_status |= MID_ESP_STATUS_INVALID_FW_VERSION;
		return -1;
	}

	mid_session_version_lr_t lr_ver;
	uint8_t identifiers[3];

	if (mid_get_software_identifiers(identifiers)) {
		lr_ver = (mid_session_version_lr_t) { identifiers[0], identifiers[1], identifiers[2] };
		mid_status &= ~MID_ESP_STATUS_INVALID_LR_VERSION;
	} else {
		mid_status |= MID_ESP_STATUS_INVALID_LR_VERSION;
		return -1;
	}

	midlts_err_t err;
	if ((err = mid_session_init(&mid_lts, fw_ver, lr_ver)) != LTS_OK) {
		mid_status |= MID_ESP_STATUS_LTS;
	} else {
		mid_status &= ~MID_ESP_STATUS_LTS;
	}

	return mid_status ? -1 : 0;
}
