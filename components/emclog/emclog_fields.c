#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "emclog.h"

void emclogger_datetime(char *buf, size_t size) {
    udatetime_t dt;
    utz_datetime_init_utc(&dt);
    utz_datetime_format_iso(buf, size, &dt);
}

int emclogger_counter(void) {
	static int counter = 0;
	return counter++;
}

int emclogger_rssi(void) {
	int rssi = 0;
    wifi_ap_record_t wifidata;
    if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
        rssi = (int)wifidata.rssi;
    }
	return rssi;
}

int emclogger_wifipower(void) {
    int8_t power = 0;
    esp_wifi_get_max_tx_power(&power);
	return power;
}

float emclogger_v0(void) { return MCU_GetVoltages(0); }
float emclogger_v1(void) { return MCU_GetVoltages(1); }
float emclogger_v2(void) { return MCU_GetVoltages(2); }

float emclogger_i0(void) { return MCU_GetCurrents(0); }
float emclogger_i1(void) { return MCU_GetCurrents(1); }
float emclogger_i2(void) { return MCU_GetCurrents(2); }

int emclogger_emt0(void) { return MCU_GetEmeterTemperature(0); }
int emclogger_emt1(void) { return MCU_GetEmeterTemperature(1); }
int emclogger_emt2(void) { return MCU_GetEmeterTemperature(2); }

int emclogger_pt0(void) { return MCU_GetTemperaturePowerBoard(0); }
int emclogger_pt1(void) { return MCU_GetTemperaturePowerBoard(1); }

int emclogger_sht30(void) { return I2CGetSHT30Temperature(); }

uint32_t emclogger_esp_reset(void) { return (uint32_t)esp_reset_reason(); }
uint32_t emclogger_mcu_reset(void) { return (uint32_t)MCU_GetResetSource(); }

int emclogger_grid_type(void) { return (int)MCU_GetGridType(); }
int emclogger_cable_type(void) { return (int)MCU_GetCableType(); }

int emclogger_charge_mode(void) { return (int)MCU_GetChargeMode(); }
int emclogger_charge_op_mode(void) { return (int)MCU_GetChargeOperatingMode(); }

void emclogger_register_fields(EmcLogger *logger) {
	emclogger_add_str(logger, "Time", emclogger_datetime, EMC_FLAG_NONE);

	emclogger_add_float(logger, "V0", emclogger_v0, EMC_FLAG_NONE);
	emclogger_add_float(logger, "V1", emclogger_v1, EMC_FLAG_NONE);
	emclogger_add_float(logger, "V2", emclogger_v2, EMC_FLAG_NONE);

	emclogger_add_float(logger, "I0", emclogger_i0, EMC_FLAG_NONE);
	emclogger_add_float(logger, "I1", emclogger_i1, EMC_FLAG_NONE);
	emclogger_add_float(logger, "I2", emclogger_i2, EMC_FLAG_NONE);

	emclogger_add_float(logger, "Power", MCU_GetPower, EMC_FLAG_NONE);

	emclogger_add_int(logger, "ET0", emclogger_emt0, EMC_FLAG_NONE);
	emclogger_add_int(logger, "ET1", emclogger_emt1, EMC_FLAG_NONE);
	emclogger_add_int(logger, "ET2", emclogger_emt2, EMC_FLAG_NONE);

	emclogger_add_int(logger, "PT0", emclogger_pt0, EMC_FLAG_NONE);
	emclogger_add_int(logger, "PT1", emclogger_pt1, EMC_FLAG_NONE);

	emclogger_add_int(logger, "SHT30", emclogger_sht30, EMC_FLAG_NONE);
	emclogger_add_int(logger, "GridType", emclogger_grid_type, EMC_FLAG_NONE);
	emclogger_add_int(logger, "CableType", emclogger_cable_type, EMC_FLAG_NONE);
	emclogger_add_int(logger, "ChargeMode", emclogger_charge_mode, EMC_FLAG_NONE);
	emclogger_add_int(logger, "ChargeOpMode", emclogger_charge_op_mode, EMC_FLAG_NONE);

	emclogger_add_int(logger, "WifiRSSI", emclogger_rssi, EMC_FLAG_NONE);
	emclogger_add_int(logger, "WifiPower", emclogger_wifipower, EMC_FLAG_NONE);

	emclogger_add_uint32(logger, "Warnings", MCU_GetWarnings, EMC_FLAG_HEX);
	emclogger_add_uint32(logger, "MCUResetSource", emclogger_mcu_reset, EMC_FLAG_HEX);
	emclogger_add_uint32(logger, "ESPResetSource", emclogger_esp_reset, EMC_FLAG_HEX);

	emclogger_add_int(logger, "Counter", emclogger_counter, EMC_FLAG_NONE);
}
