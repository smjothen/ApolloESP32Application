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
#include "calibration_emeter.h"

static const char *TAG = "EMETER         ";

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

bool emeter_write(uint8_t reg, int registerValue) {
	uint32_t combined = ((uint8_t)reg) << 24 | (registerValue & 0xFFFFFF);
	MessageType type = MCU_SendUint32Parameter(ParamCalibrationSetParameter, combined);
	if (type != MsgWriteAck) {
		return false;
	}
	return true;
}

bool emeter_write_float(uint8_t reg, double value, int radix) {
	int registerValue = floatToSn(value, radix);
	return emeter_write(reg, registerValue);
}

bool emeter_read(uint8_t reg, uint32_t *val) {
	ZapMessage msg = MCU_SendUint8WithReply(ParamCalibrationReadParameter, reg);
	if (msg.identifier != ParamCalibrationReadParameter || msg.type != MsgWriteAck || msg.length != 4) {
		return false;
	}
	*val = GetUint32_t(msg.data);
	return true;
}

double emeter_get_fsv(void) {
	// Should probably fix this to get ~1.0 gains
	return 435.54;
}

double emeter_get_fsi(void) {
	return 159.16;
}
