#ifndef __MID_H__
#define __MID_H__

#include "uuid.h"
#include "mid_event.h"

#include <stdint.h>
#include <stdbool.h>

//#define MID_DEVELOPMENT_MODE

#ifdef MID_DEVELOPMENT_MODE

#warning "MID development mode! Should not be used in production firmware!"

#define MID_ALLOW_KEY_GENERATION
#define MID_ALLOW_LITTLEFS_FORMAT

#endif

#define MID_ESP_STATUS_KEY 1
#define MID_ESP_STATUS_EVENT_LOG 2
#define MID_ESP_STATUS_FILESYSTEM 4
#define MID_ESP_STATUS_INVALID_FW_VERSION 8
#define MID_ESP_STATUS_INVALID_LR_VERSION 16
#define MID_ESP_STATUS_LTS 32

typedef struct {
	uint32_t status;
	uint32_t watt_hours;
	uint8_t identifiers[3];
	uint8_t reserved; // Word-align
} mid_package_t;

int mid_init(const char *fw_version);
uint32_t mid_get_esp_status(void);

bool mid_get_package(mid_package_t *pkg);
bool mid_get_status(uint32_t *status);
bool mid_get_watt_hours(uint32_t *watt_hours);
bool mid_get_software_identifiers(uint8_t identifiers[3]);

bool mid_get_calibration_id(uint32_t *id);
bool mid_set_blink_enabled(bool enabled);
bool mid_get_energy_interpolated(float *energy);
bool mid_get_is_calibration_handle(void);

bool mid_session_is_open(void);

int mid_session_event_uuid(uuid_t uuid);
int mid_session_event_auth_cloud(const char *data);
int mid_session_event_auth_ble(const char *data);
int mid_session_event_auth_rfid(const char *data);
int mid_session_event_auth_iso15118(const char *data);
int mid_session_event_open(void);
int mid_session_event_close(void);
int mid_session_event_tariff(void);

#endif
