
#include <stdio.h>

#include "driver/i2c.h"

#include "i2cInterface.h"


///* SHT30 internal registers  */
//#define SHT30_SAMPLING_FREQUENCY        0x2236//0x2126
//#define SHT30_PERIODIC_MEASUREMENT_2HZ  0xE000
//#define SHT30_READ_STATUS_REGISTER      0xF32D

static float internalTemperature = 0.0;
static float internalHumidity = 0.0;
static uint8_t slaveAddressSHT30 = 0x44;

esp_err_t SHT30Init()
{
    uint8_t writeBytes[2] = {0};
    writeBytes[0] = 0x22;
    writeBytes[1] = 0x36;
    esp_err_t err = i2c_master_write_slave(slaveAddressSHT30, (uint8_t*)&writeBytes, 2);

    return err;
}


float SHT30ReadTemperature()
{
    uint8_t readBytes[6] = {0};

    unsigned int rawTemperature = 0;
    unsigned int rawHumidity = 0;

	i2c_master_read_slave(slaveAddressSHT30, readBytes, 6);

	rawTemperature = (readBytes[0] << 8) + readBytes[1];
	internalTemperature = -45 + (175 * (rawTemperature/65535.0));

	rawHumidity = (readBytes[3] << 8) + readBytes[4];
	internalHumidity = 100 * rawHumidity / 65535.0;

    return internalTemperature;
}


float SHT30ReadHumidity()
{
	return internalHumidity;
}


