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
#include "uuid.h"

#include "mid_active.h"
#include "mid_sign.h"
#include "mid_ocmf.h"
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
static mid_sign_ctx_t mid_sign = {0};
static const char *mid_serial = NULL;

uint32_t mid_get_esp_status(void) {
	return mid_status;
}

int mid_init(const char *serial, const char *fw_version) {
	if (!serial || !fw_version) {
		return -1;
	}

	mid_serial = serial;

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

	if (mid_sign_ctx_init(&mid_sign, storage_Get_MIDPrivateKey(), storage_Get_MIDPublicKey()) != 0) {
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
			mid_event_log_free(&log);
		} else {
			ESP_LOGE(TAG, "Failure to read MID event log!");
			mid_status |= MID_ESP_STATUS_EVENT_LOG;
			mid_event_log_free(&log);
			return -1;
		}
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

	ESP_LOGI(TAG, "MID FW Version: %" PRIu16 ".%" PRIu16 ".%" PRIu16 ".%" PRIu16, fw_ver.major, fw_ver.minor, fw_ver.patch, fw_ver.extra);

	mid_session_version_lr_t lr_ver;
	uint8_t identifiers[3];

	if (mid_get_software_identifiers(identifiers)) {
		lr_ver = (mid_session_version_lr_t) { identifiers[0], identifiers[1], identifiers[2] };
		mid_status &= ~MID_ESP_STATUS_INVALID_LR_VERSION;
	} else {
		mid_status |= MID_ESP_STATUS_INVALID_LR_VERSION;
		return -1;
	}

	ESP_LOGI(TAG, "MID LR Version: %" PRIu8 ".%" PRIu8 ".%" PRIu8, lr_ver.major, lr_ver.minor, lr_ver.patch);

	midlts_err_t err;
	if ((err = mid_session_init(&mid_lts, fw_ver, lr_ver)) != LTS_OK) {
		mid_status |= MID_ESP_STATUS_LTS;
		return -1;
	} else {
		mid_status &= ~MID_ESP_STATUS_LTS;
	}

	return mid_status ? -1 : 0;
}

mid_session_meter_value_flag_t mid_get_time_status(void) {
	// TODO: Check if we've synced recently and it is relatively reliable (compare with
	// esp_timer for example), or if we've booted offline and are using RTC time, etc
	return MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE;
}

typedef midlts_err_t (*mid_session_event_t)(midlts_ctx_t *, midlts_pos_t *, mid_session_record_t *, const struct timespec, mid_session_meter_value_flag_t, uint32_t);

typedef enum {
	MID_ERR_FATAL = 1,
	MID_ERR_SESSION_ALREADY_OPEN = 2,
	MID_ERR_SESSION_NOT_OPEN = 4,
} miderr_t;

int mid_session_add_event(mid_session_event_t event, midlts_pos_t *pos, mid_session_meter_value_flag_t type_flag) {
	if (mid_status) {
		ESP_LOGE(TAG, "Can't add session event: Status %08" PRIu32, mid_status);
		return -1;
	}

	mid_package_t pkg;
	if (!mid_get_package(&pkg)) {
		ESP_LOGE(TAG, "Can't add session event: MID Package");
		return -1;
	}

	int ret;
	struct timespec ts;

	if ((ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0) {
		ESP_LOGE(TAG, "Can't add session event: Time");
		return -1;
	}

	mid_session_meter_value_flag_t flag = mid_get_time_status() | type_flag;

	uint32_t watt_hours = 0;

	uint32_t status = pkg.status;
	status &= ~(MID_STATUS_INVALID_FR | MID_STATUS_NOT_CALIBRATED | MID_STATUS_NOT_VERIFIED | MID_STATUS_INVALID_BOOTLOADER);

	if (status) {
		flag |= MID_SESSION_METER_VALUE_FLAG_METER_ERROR;
	} else {
		watt_hours = pkg.watt_hours;
	}

	// TODO: Use semaphore to serialize events
	//
	mid_session_record_t rec;
	midlts_err_t err;
	if ((err = event(&mid_lts, pos, &rec, ts, flag, watt_hours)) != LTS_OK) {
		ESP_LOGE(TAG, "Can't add session event: LTS");
		return -1;
	}

	return 0;
}

bool mid_session_is_open(void) {
	return MID_SESSION_IS_OPEN(&mid_lts);
}

int mid_session_get_session_id(uint32_t *out) {
	if (mid_session_is_open()) {
		*out = mid_lts.active_session.pos.u32;
		return 0;
	}
	return -1;
}

int mid_session_event_open(uint32_t *out) {
	midlts_pos_t pos;
	if (mid_session_add_event(mid_session_add_open, &pos, MID_SESSION_METER_VALUE_READING_FLAG_START) < 0) {
		return -1;
	}
	*out = pos.u32;
	return 0;
}

int mid_session_event_close(uint32_t *out) {
	midlts_pos_t pos;
	if (mid_session_add_event(mid_session_add_close, &pos, MID_SESSION_METER_VALUE_READING_FLAG_END) < 0) {
		return -1;
	}
	*out = pos.u32;
	return 0;
}

int mid_session_event_tariff(uint32_t *out) {
	midlts_pos_t pos;
	if (mid_session_add_event(mid_session_add_tariff, &pos, MID_SESSION_METER_VALUE_READING_FLAG_TARIFF) < 0) {
		return -1;
	}
	*out = pos.u32;
	return 0;
}

// NOTE: Session must be verified to be open when calling this!
int mid_session_event_uuid(uuid_t uuid) {
	if (mid_status) {
		ESP_LOGE(TAG, "Can't add session metadata: Status %08" PRIu32, mid_status);
		return -1;
	}

	int ret;
	struct timespec ts;

	if ((ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0) {
		ESP_LOGE(TAG, "Can't add session metadata: Time");
		return -1;
	}

	uint8_t buf[16];
	uuid_to_bytes(uuid, buf);

	midlts_err_t err;
	if ((err = mid_session_add_id(&mid_lts, NULL, NULL, ts, buf)) != LTS_OK) {
		ESP_LOGE(TAG, "Can't add session metadata: LTS");
		return -1;
	}

	return 0;
}

static int mid_session_metadata_auth_uuid(mid_session_auth_source_t source, uuid_t uuid) {
	if (mid_status) {
		ESP_LOGE(TAG, "Can't add session metadata: Status %08" PRIu32, mid_status);
		return -1;
	}

	int ret;
	struct timespec ts;

	if ((ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0) {
		ESP_LOGE(TAG, "Can't add session metadata: Time");
		return -1;
	}

	uint8_t len = 16;
	uint8_t data[20];
	uuid_to_bytes(uuid, data);

	midlts_err_t err;
	if ((err = mid_session_add_auth(&mid_lts, NULL, NULL, ts, source, MID_SESSION_AUTH_TYPE_UUID, data, len)) != LTS_OK) {
		ESP_LOGE(TAG, "Can't add session metadata: LTS");
		return -1;
	}

	return 0;
}

static int mid_session_metadata_auth_rfid(mid_session_auth_source_t source, uint8_t *data, uint8_t len) {
	if (mid_status) {
		ESP_LOGE(TAG, "Can't add session metadata: Status %08" PRIu32, mid_status);
		return -1;
	}

	int ret;
	struct timespec ts;

	if ((ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0) {
		ESP_LOGE(TAG, "Can't add session metadata: Time");
		return -1;
	}

	midlts_err_t err;
	if ((err = mid_session_add_auth(&mid_lts, NULL, NULL, ts, source, MID_SESSION_AUTH_TYPE_RFID, data, len)) != LTS_OK) {
		ESP_LOGE(TAG, "Can't add session metadata: LTS");
		return -1;
	}

	return 0;
}

static int mid_session_metadata_auth_string(mid_session_auth_source_t source, uint8_t *data, uint8_t len) {
	if (mid_status) {
		ESP_LOGE(TAG, "Can't add session metadata: Status %08" PRIu32, mid_status);
		return -1;
	}

	int ret;
	struct timespec ts;

	if ((ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0) {
		ESP_LOGE(TAG, "Can't add session metadata: Time");
		return -1;
	}

	midlts_err_t err;
	if ((err = mid_session_add_auth(&mid_lts, NULL, NULL, ts, source, MID_SESSION_AUTH_TYPE_STRING, data, len)) != LTS_OK) {
		ESP_LOGE(TAG, "Can't add session metadata: LTS");
		return -1;
	}

	return 0;
}

static bool hex_to_bytes(const char *in, size_t inlen, uint8_t *out, size_t *outlen) {
	if (inlen % 2 != 0) {
		ESP_LOGE(TAG, "Should be even amount of nibbles!");
		return -1;
	}

	// Simple validation
	for (size_t i = 0; i < inlen; i++) {
		if (!isxdigit((int)in[i])) {
			ESP_LOGE(TAG, "Non-hex digit in hex string!");
			return -1;
		}
	}

	size_t len = 0;
	for (uint8_t i = 0; i < inlen; i += 2) {
		if (sscanf(&in[i], "%02" SCNx8, &out[i / 2]) != 1) {
			ESP_LOGE(TAG, "Error scanning hex string!");
			return -1;
		}
		len++;
	}

	*outlen = len;
	return 0;
}

static int mid_session_event_auth_internal(mid_session_auth_source_t source, const char *data) {
	// Rudimentary data parsing, assuming 4 byte prefix and dash (nfc- / ble-)

	size_t len = strlen(data);
	if (len < 4) {
		ESP_LOGE(TAG, "Can't add session metadata: Invalid");
		return -1;
	}

	len = len - 4;
	data = data + 4;

	// UUID
	if (len == 36) {
		uuid_t uuid;
		if (!uuid_from_string(&uuid, data)) {
			ESP_LOGE(TAG, "Can't add session metadata: UUID");
			return -1;
		}
		return mid_session_metadata_auth_uuid(source, uuid);
	}

	// RFID
	if (len == 8 * 2 ||
		len == 4 * 2 ||
		len == 7 * 2 ||
		len == 10 * 2) {

		uint8_t out[20];
		size_t outlen = 0;
		if (!hex_to_bytes(data, len, out, &outlen)) {
			ESP_LOGE(TAG, "Can't add session metadata: Hex");
			return -1;
		}
		return mid_session_metadata_auth_rfid(source, out, outlen);
	}

	// TODO: Will data have separate prefix for ISO15118 auth modes? How to detect?

	if (len > 20) {
		len = 20;
	}
	return mid_session_metadata_auth_string(source, (uint8_t *)data, len);
}

// Different sources of authentication
int mid_session_event_auth_cloud(const char *data) {
	return mid_session_event_auth_internal(MID_SESSION_AUTH_SOURCE_CLOUD, data);
}

int mid_session_event_auth_ble(const char *data) {
	return mid_session_event_auth_internal(MID_SESSION_AUTH_SOURCE_BLE, data);
}

int mid_session_event_auth_rfid(const char *data) {
	return mid_session_event_auth_internal(MID_SESSION_AUTH_SOURCE_RFID, data);
}

int mid_session_event_auth_iso15118(const char *data) {
	return mid_session_event_auth_internal(MID_SESSION_AUTH_SOURCE_ISO15118, data);
}

const char *mid_session_sign_session(uint32_t id, double *energy) {
	midlts_pos_t pos;
	pos.u32 = id;

	midlts_err_t err;
	if ((err = mid_session_read_session(&mid_lts, &pos)) != LTS_OK) {
		ESP_LOGE(TAG, "Error reading session ID %" PRIu32 " : %d", id, err);
		return NULL;
	}

	midlts_active_session_get_energy(&mid_lts.query_session, energy);
	return midocmf_signed_transaction_from_active_session(&mid_sign, mid_serial, &mid_lts.query_session);
}

const char *mid_session_sign_current_session(double *energy) {
	midlts_active_session_get_energy(&mid_lts.active_session, energy);
	return midocmf_signed_transaction_from_active_session(&mid_sign, mid_serial, &mid_lts.active_session);
}
