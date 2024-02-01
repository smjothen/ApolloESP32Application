#ifndef __MID_H__
#define __MID_H__

#include "mid_event.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint32_t status;
	uint32_t watt_hours;
	uint8_t identifiers[3];
	uint8_t reserved; // Word-align
} mid_package_t;

int mid_init(void);
uint32_t mid_get_esp_status(void);

bool mid_get_package(mid_package_t *pkg);
bool mid_get_status(uint32_t *status);
bool mid_get_watt_hours(uint32_t *watt_hours);
bool mid_get_software_identifiers(uint8_t identifiers[3]);

bool mid_get_calibration_id(uint32_t *id);
bool mid_set_blink_enabled(bool enabled);
bool mid_get_energy_interpolated(float *energy);
bool mid_get_is_calibration_handle(void);

bool mid_get_event_log(mid_event_log_t *);

#endif
