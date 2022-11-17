
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "../../main/DeviceInfo.h"

void i2c_ctrl_debug(int state);
struct DeviceInfo i2cGetLoadedDeviceInfo();
bool i2cCheckSerialForDiskPartition();
void i2cSetDebugDeviceInfoToMemory(struct DeviceInfo debugDevInfo);
bool i2CDeviceInfoIsLoaded();
void I2CDevicesInit();
uint8_t i2cIsAuthenticated();
void i2cClearAuthentication();

esp_err_t i2cWriteDeviceInfoToEEPROM(struct DeviceInfo newDeviceInfo);
struct DeviceInfo i2cReadDeviceInfoFromEEPROM();
bool i2cRTCChecked();

float I2CGetSHT30Temperature();
float I2CGetSHT30Humidity();

esp_err_t I2CCalibrateCoverProximity();

int I2CGetStackWatermark();
void I2CDevicesStartTask();

uint8_t deviceInfoVersionOnEeprom();
uint32_t GetPassedDetectedCounter();
uint32_t GetFailedDetectedCounter();

void i2cFlagNewTimeWrite();
void i2cSetNFCTagPairing(bool pairingState);

#ifdef __cplusplus
}
#endif
