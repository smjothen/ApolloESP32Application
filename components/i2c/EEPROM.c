
#include <stdio.h>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "i2cInterface.h"
#include "EEPROM.h"

//static const char *TAG = "EEPROM   ";

//EEPROM-CAT24C04
static uint8_t slaveAddressEEPROM = 0x56;


esp_err_t EEPROM_Read()
{
	uint8_t readBytes[16] = {0};

	uint8_t readAddr = 0;
	esp_err_t err = ESP_OK;
	for (uint8_t line = 0; line <= 31; line++)
	{
		printf("#%02d:  ", line);

		err = i2c_master_read_slave_at_address(slaveAddressEEPROM, readAddr, readBytes, 16);
		for (int i = 0; i <= 15; i++)
			printf(" %03d", readBytes[i]);

		printf("\n");
		readAddr += 16;
	}

	printf("\n");

	return err;
}

esp_err_t EEPROM_Write()
{

//	    writeBytes[0] = 0x00;
//	    writeBytes[1] = 0x01;
//	    writeBytes[2] = 0x02;
//	    i2c_master_write_slave(I2C_NUM_0, &writeBytes, 3);
//	    printf("Wrote: 0x01 0x02");

	uint8_t writeBytes[16] = {0};

	//WRITE
	uint8_t startAddr = 0;
	//uint8_t writeLine[16] = {0};
	uint8_t valueIncrementor = 0;
	//uint8_t line = 0;
	for (uint8_t line = 0; line <= 31; line++)
	{

		for (uint8_t lineByte = 0; lineByte <= 15; lineByte++)
		{
//	    		if(line == 0)
			writeBytes[lineByte]=valueIncrementor;
			valueIncrementor++;
//	    		if(line == 1)
//					writeBytes[lineByte]=22;
//	    		if(line == 2)
//	    			writeBytes[lineByte]=33;
		}
		i2c_master_write_slave_at_address(slaveAddressEEPROM, startAddr, (uint8_t*)&writeBytes, 16);
		startAddr += 16;
		vTaskDelay(100 / portTICK_PERIOD_MS);
		printf("Wrote line #%d\n", line);
	}

	printf("\n");

	/*while(true)
	{
		readAddr = 0;
		for (uint8_t line = 0; line <= 31; line++)
		{
			printf("#%02d:   ", line);
			i2c_master_read_slave_at_address(slaveAddressEEPROM, readAddr, readBytes, 16);
			for (int i = 0; i <= 15; i++)
			{
				printf(" %03d", readBytes[i]);
			}
			readAddr += 16;

			printf("\n");
		}

		printf("\n");


	    vTaskDelay(10000 / portTICK_PERIOD_MS);

	}*/

	return 0;
}


//For testing the EEPROM
void EEPROM_WriteFullTest()
{
	uint8_t readBytes[16] = {0};
	uint8_t writeBytes[16] = {0};

	//READ
	uint8_t readAddr = 0;
	for (uint8_t line = 0; line <= 31; line++)
	{
		printf("#%02d:  ", line);

		i2c_master_read_slave_at_address(slaveAddressEEPROM, readAddr, readBytes, 16);
		for (int i = 0; i <= 15; i++)
			printf(" %03d", readBytes[i]);

		printf("\n");
		readAddr += 16;
	}

	printf("\n");



//	    writeBytes[0] = 0x00;
//	    writeBytes[1] = 0x01;
//	    writeBytes[2] = 0x02;
//	    i2c_master_write_slave(I2C_NUM_0, &writeBytes, 3);
//	    printf("Wrote: 0x01 0x02");

	//WRITE
	uint8_t startAddr = 0;
	//uint8_t writeLine[16] = {0};
	uint8_t valueIncrementor = 0;
	//uint8_t line = 0;
	for (uint8_t line = 0; line <= 31; line++)
	{

		for (uint8_t lineByte = 0; lineByte <= 15; lineByte++)
		{
//	    		if(line == 0)
			writeBytes[lineByte]=valueIncrementor;
			valueIncrementor++;
//	    		if(line == 1)
//					writeBytes[lineByte]=22;
//	    		if(line == 2)
//	    			writeBytes[lineByte]=33;
		}
		i2c_master_write_slave_at_address(I2C_NUM_0, startAddr, (uint8_t*)&writeBytes, 16);
		startAddr += 16;
		vTaskDelay(100 / portTICK_PERIOD_MS);
		printf("Wrote line #%d\n", line);
	}

	printf("\n");

	while(true)
	{

		readAddr = 0;
		for (uint8_t line = 0; line <= 31; line++)
		{
			printf("#%02d:   ", line);
			i2c_master_read_slave_at_address(I2C_NUM_0, readAddr, readBytes, 16);
			for (int i = 0; i <= 15; i++)
			{
				printf(" %03d", readBytes[i]);
			}
			readAddr += 16;

			printf("\n");
		}

		printf("\n");


    	vTaskDelay(10000 / portTICK_PERIOD_MS);

	}

}
