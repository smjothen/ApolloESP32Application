#include <string.h>
#include <sys/param.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "protocol_task.h"
#include "calibration.h"
#include "calibration_emeter.h"

//static const char *TAG = "CALIBRATION    ";

double snToFloat(uint32_t data, uint16_t radix) {
    // Copy 24 bit sign to 32 bit sign
    if(data & 0x800000) {
        return -((double) (0xFFFFFF + 1 - data) / (1UL << radix));
    }
    
    return (double) data / (1UL << radix);
}

uint32_t floatToSn(double data, uint16_t radix) {
    if(data < 0.0) {
        return (0xFFFFFF + 1 - (-data * (1UL << radix)));
    }
    
    return data * (1UL << radix);
}

bool emeter_write(uint8_t reg, uint32_t registerValue) {
	uint32_t combined = (reg << 24) | (registerValue & 0xFFFFFF);
	MessageType type = MCU_SendUint32Parameter(ParamCalibrationSetParameter, combined);
	if (type != MsgWriteAck) {
		return false;
	}
	return true;
}

bool emeter_write_float(uint8_t reg, double value, uint8_t radix) {
	uint32_t registerValue = floatToSn(value, radix);
	return emeter_write(reg, registerValue);
}

bool emeter_read(uint8_t reg, uint32_t *val) {
#ifdef CONFIG_CAL_SIMULATION

	// Only used directly for VOFFS calibration, so simulate a small offset
	*val = floatToSn(0.000187, 23);
	return true;

#endif

	ZapMessage msg = MCU_SendUint8WithReply(ParamCalibrationReadParameter, reg);
	if (msg.identifier != ParamCalibrationReadParameter || msg.type != MsgWriteAck || msg.length != 4) {
		return false;
	}
	*val = GetUint32_t(msg.data);
	return true;
}

#define FSV_POWER2 (435.54 * 1.406)
#define FSI_POWER2 (159.16 * 1.986)

#define FSV_POWER1 (435.54 * 0.988)
#define FSI_POWER1 (159.16 * 2.037)
 
double emeter_get_fsv(void) {
	switch(MCU_GetHwIdMCUPower()) {
		case HW_POWER_UNKNOWN:
		case HW_POWER_1:
			return FSV_POWER1;
		case HW_POWER_2:
		case HW_POWER_3_UK:
		case HW_POWER_4_X804:
		case HW_POWER_5_UK_X804:
			return FSV_POWER2;
	}

	return FSV_POWER2;
}

double emeter_get_fsi(void) {
	switch(MCU_GetHwIdMCUPower()) {
		case HW_POWER_UNKNOWN:
		case HW_POWER_1:
			return FSI_POWER1;
		case HW_POWER_2:
		case HW_POWER_3_UK:
		case HW_POWER_4_X804:
		case HW_POWER_5_UK_X804:
			return FSI_POWER2;
	}

	return FSI_POWER2;
}
