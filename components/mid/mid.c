#include <stdio.h>

#include "mid_status.h"
#include "mid.h"
#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"

bool mid_get_package(MIDPackage *pkg) {
	ZapMessage msg = MCU_ReadParameter(SignedMeterValue);
	if (msg.length != sizeof (MIDPackage) || msg.identifier != SignedMeterValue) {
		return false;
	}

	*pkg = *(MIDPackage *)msg.data;
	return true;
}

bool mid_get_status(uint32_t *status) {
	MIDPackage pkg;
	if (mid_get_package(&pkg)) {
		*status = pkg.status;
		return true;
	}
	return false;
}

bool mid_get_watt_hours(uint32_t *watt_hours) {
	MIDPackage pkg;
	if (mid_get_package(&pkg)) {
		*watt_hours = pkg.wattHours;
		return true;
	}
	return false;
}

bool mid_get_software_identifiers(uint8_t identifiers[3]) {
	MIDPackage pkg;
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
