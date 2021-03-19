
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "../../main/DeviceInfo.h"

void i2c_ctrl_debug(int state);
struct DeviceInfo i2cGetLoadedDeviceInfo();
void i2cSetDebugDeviceInfoToMemory(struct DeviceInfo debugDevInfo);
bool i2CDeviceInfoIsLoaded();
void I2CDevicesInit();

esp_err_t i2cWriteDeviceInfoToEEPROM(struct DeviceInfo newDeviceInfo);
struct DeviceInfo i2cReadDeviceInfoFromEEPROM();
bool i2cRTCChecked();

float I2CGetSHT30Temperature();
float I2CGetSHT30Humidity();

int I2CGetStackWatermark();
void I2CDevicesStartTask();

uint8_t deviceInfoVersionOnEeprom();
uint32_t GetPassedDetectedCounter();
uint32_t GetFailedDetectedCounter();

void i2cFlagNewTimeWrite();

#ifdef __cplusplus
}
#endif
