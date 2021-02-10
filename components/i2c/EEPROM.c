
#include <stdio.h>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "i2cInterface.h"
#include "EEPROM.h"
#include <string.h>
static const char *TAG = "EEPROM";

//EEPROM-CAT24C04
static uint8_t slaveAddressEEPROM = 0x56;


esp_err_t EEPROM_Read()
{
	uint8_t readBytes[16] = {0};

	uint16_t readAddr = 0;
	esp_err_t err = ESP_OK;
	//for (uint8_t line = 0; line <= 31; line++)
	for (uint8_t line = 0; line <= 6; line++)
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
	uint16_t startAddr = 0;
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

#define EEPROM_FORMAT_VERSON       0 // 1 byte  ->  0]
#define EEPROM_FACTORY_STAGE       1 // 1 byte  ->  1]
#define EEPROM_ADDR_SERIAL_NUMBER 16 //10 bytes -> 25]
#define EEPROM_ADDR_PSK_1_3 	  32 //16 bytes -> 47]
#define EEPROM_ADDR_PSK_2_3 	  48 //16 bytes -> 63]
#define EEPROM_ADDR_PSK_3_3 	  64 //13 bytes -> 76]
#define EEPROM_ADDR_PIN 		  80 // 5 bytes -> 84]

esp_err_t EEPROM_ReadFormatVersion(uint8_t *formatVersionToRead)
{
	volatile esp_err_t err = i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_FORMAT_VERSON, formatVersionToRead, 1);
	return err;
}

esp_err_t EEPROM_ReadFactroyStage(uint8_t *factory_stage){
	return i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_FACTORY_STAGE, factory_stage, 1);
}

esp_err_t _eeprom_write_byte(uint8_t byte_to_write, uint16_t address){
	esp_err_t write_error = i2c_master_write_slave_at_address(slaveAddressEEPROM, address, &byte_to_write, 1);
	if(write_error != ESP_OK){
		ESP_LOGW(TAG, "failed i2c_master_write_slave_at_address with %d", write_error);
		return write_error;
	}

	vTaskDelay(100 / portTICK_PERIOD_MS);

	uint8_t readback_byte = 0;
	esp_err_t read_error = i2c_master_read_slave_at_address(slaveAddressEEPROM, address, &readback_byte, 1);
	if(read_error != ESP_OK){
		ESP_LOGW(TAG, "failed i2c_master_read_slave_at_address with %d", read_error);
		return read_error;
	}

	if(readback_byte == byte_to_write)
		return ESP_OK;
	else
		ESP_LOGW(TAG, "Readback error %d and %d", readback_byte, byte_to_write);
		return ESP_FAIL;
}

esp_err_t _eeprom_write_byte_with_retires(uint8_t byte_to_write, uint16_t address){
	for(int retry=0; retry<3; retry++){
		esp_err_t result = _eeprom_write_byte(byte_to_write, address);
		if(result == ESP_OK){
			return ESP_OK;
		}
	}

	return ESP_FAIL;
}

esp_err_t EEPROM_WriteFormatVersion(uint8_t formatVersionToWrite)
{
	return _eeprom_write_byte(formatVersionToWrite, EEPROM_FORMAT_VERSON);
}


esp_err_t EEPROM_WriteFactoryStage(uint8_t stage)
{
	return _eeprom_write_byte_with_retires(stage, EEPROM_FACTORY_STAGE);
}

esp_err_t EEPROM_ReadSerialNumber(char * serianNumberToRead)
{
	esp_err_t err = i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_SERIAL_NUMBER, (uint8_t*)serianNumberToRead, 10);
	return err;
}

esp_err_t EEPROM_WriteSerialNumber(char * serialNumberToWrite)
{
	i2c_master_write_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_SERIAL_NUMBER, (uint8_t*)serialNumberToWrite, 10);

	vTaskDelay(100 / portTICK_PERIOD_MS);

	char serialNumberToRead[10] = {0};
	i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_SERIAL_NUMBER, (uint8_t*)serialNumberToRead, 10);
	int cmp = strcmp(serialNumberToWrite, serialNumberToRead);

	return cmp;
}



esp_err_t EEPROM_ReadPSK(char * PSKToRead)
{
	esp_err_t err = i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_1_3, (uint8_t*)PSKToRead, 16);
	err += i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_2_3, (uint8_t*)&PSKToRead[16], 16);
	err += i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_3_3, (uint8_t*)&PSKToRead[32], 13);
	return err;
}

esp_err_t EEPROM_WritePSK(char * PSKToWrite)
{
	i2c_master_write_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_1_3, (uint8_t*)PSKToWrite, 16);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	char PSKToRead[45] = {0};
	i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_1_3, (uint8_t*)PSKToRead, 16);

	i2c_master_write_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_2_3, (uint8_t*)&PSKToWrite[16], 16);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_2_3, (uint8_t*)&PSKToRead[16], 16);

	i2c_master_write_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_3_3, (uint8_t*)&PSKToWrite[32], 13);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	//volatile char PSKToRead[16] = {0};
	i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PSK_3_3, (uint8_t*)&PSKToRead[32], 13);

	int cmp = strcmp(PSKToWrite, PSKToRead);

	return cmp;
}


esp_err_t EEPROM_ReadPin(char * pinToRead)
{
	esp_err_t err = i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PIN, (uint8_t*)pinToRead, 5);
	return err;
}

esp_err_t EEPROM_WritePin(char * pinToWrite)
{
	i2c_master_write_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PIN, (uint8_t*)pinToWrite, 5);

	vTaskDelay(100 / portTICK_PERIOD_MS);

	char pinToRead[10] = {0};
	i2c_master_read_slave_at_address(slaveAddressEEPROM, EEPROM_ADDR_PIN, (uint8_t*)pinToRead, 5);
	int cmp = strcmp(pinToWrite, pinToRead);

	return cmp;
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
		i2c_master_write_slave_at_address(slaveAddressEEPROM, startAddr, (uint8_t*)&writeBytes, 16);
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

	}

}



void EEPROM_Erase()
{
	uint8_t writeBytes[16];
	memset(writeBytes, 0xFF, 16);

	uint16_t startAddr = 0;

	for (uint8_t line = 0; line <= 31; line++)
	{
		i2c_master_write_slave_at_address(slaveAddressEEPROM, startAddr, (uint8_t*)writeBytes, 16);
		startAddr += 16;
		vTaskDelay(100 / portTICK_PERIOD_MS);
		printf("Wrote line #%d\n", line);
	}

	printf("\n");

	EEPROM_Read();
}
