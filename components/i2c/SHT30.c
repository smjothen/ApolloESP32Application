
#include <stdio.h>

#include "driver/i2c.h"

#include "i2cInterface.h"


///* SHT30 internal registers  */
//#define SHT30_SAMPLING_FREQUENCY        0x2236//0x2126
//#define SHT30_PERIODIC_MEASUREMENT_2HZ  0xE000
//#define SHT30_READ_STATUS_REGISTER      0xF32D

volatile static float internalTemperature = 0.0;
volatile static float internalHumidity = 0.0;
static uint8_t slaveAddressSHT30 = 0x70;

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
    uint8_t writeBytes[2] = {0};

    //Wakeup command - not required
    /*writeBytes[0] = 0x35;
	writeBytes[1] = 0x17;
    i2c_master_write_slave(slaveAddressSHT30, (uint8_t*)writeBytes, 2);*/

    //Normal mode command - with clock stretching and reading of temperature before humidity
    writeBytes[0] = 0x7C;
    writeBytes[1] = 0xA2;
    i2c_master_write_slave(slaveAddressSHT30, (uint8_t*)writeBytes, 2);

    //Wait for measurement to be performed, clock streching may not be enough
    vTaskDelay(100 / portTICK_PERIOD_MS);

    uint8_t readBytes[6] = {0};
    unsigned int rawTemperature = 0;
    unsigned int rawHumidity = 0;

    i2c_master_read_slave_with_ack(slaveAddressSHT30, readBytes, 6);

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


