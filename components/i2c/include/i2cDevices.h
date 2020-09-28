
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "../../main/DeviceInfo.h"

struct DeviceInfo i2cGetLoadedDeviceInfo();
void i2cSetDebugDeviceInfoToMemory(struct DeviceInfo debugDevInfo);
bool i2CDeviceInfoIsLoaded();
void I2CDevicesInit();
void I2CDevicesStartTask();
esp_err_t i2cWriteDeviceInfoToEEPROM(struct DeviceInfo newDeviceInfo);
struct DeviceInfo i2cReadDeviceInfoFromEEPROM();
bool i2cRTCChecked();

float I2CGetSHT30Temperature();
float I2CGetSHT30Humidity();

#ifdef __cplusplus
}
#endif