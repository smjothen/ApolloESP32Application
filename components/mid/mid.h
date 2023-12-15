#ifndef __MID_H__
#define __MID_H__

#include "mid_sign.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint32_t status;
	uint32_t wattHours;
	uint8_t identifiers[3];
	uint8_t reserved; // Word-align
} MIDPackage;

int mid_init(char *prv_pem);

bool mid_get_package(MIDPackage *pkg);
bool mid_get_status(uint32_t *status);
bool mid_get_watt_hours(uint32_t *watt_hours);
bool mid_get_software_identifiers(uint8_t identifiers[3]);

bool mid_get_calibration_id(uint32_t *id);
bool mid_set_blink_enabled(bool enabled);
bool mid_get_energy_interpolated(float *energy);
bool mid_get_is_calibration_handle(void);

#endif