
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_log.h"
#include "i2cInterface.h"
#include "CLRC661.h"
#include <string.h>

//static const char *TAG = "NFC     ";

//static uint8_t slaveAddressNFC = 0x2A;
//static uint8_t slaveAddressNFC = 0x2B;
static uint8_t slaveAddressNFC = 0x28;

uint responseDelay = 10;
bool validId = false;

struct TagInfo tagInfo;

int NFCInit()
{

    uint8_t reg = 0x7F;
    i2c_master_write_slave(slaveAddressNFC, &reg, 1);

    uint8_t message[5] = {0};

    i2c_master_read_slave(slaveAddressNFC, message, 1);

    message[0] = 0x08; //IRQ0En
	message[1] = 0x84;//0x84;//0x8C;//08;	//8=TxIRQEn
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	message[0] = 0x09; //IRQ1En
	message[1] = 0x60;//OpenDrain  alt: 0xE0;///PushPull also works
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//uint8_t regNr = 0x08;
//	i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//	i2c_master_read_slave(slaveAddressNFC, message, 1);
//	printf("reg 0x%02X: 0x%02X\n",  regNr, message[0]);
//
//	regNr = 0x09;
//	i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//	i2c_master_read_slave(slaveAddressNFC, message, 1);
//	printf("reg 0x%02X: 0x%02X\n\n",  regNr, message[0]);

	//1
	message[0] = 0x0;
	message[1] = 0x0;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//2
	message[0] = 0x02;
	message[1] = 0xB0;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//3
	message[0] = 0x05;
	message[1] = 0x00;
	message[2] = 0x00;
	i2c_master_write_slave(slaveAddressNFC, message, 3);

	//4
	message[0] = 0x00;
	message[1] = 0x0D;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//5
	message[0] = 0x02;
	message[1] = 0xB0;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//6
	message[0] = 0x28;
	message[1] = 0x8E;
	i2c_master_write_slave(slaveAddressNFC, message, 2);



	return 0;
}

static unsigned int readCount = 0;

struct TagInfo NFCGetTagInfo()
{
	return tagInfo;
}


void NFCClearTag()
{
	memset(&tagInfo, 0, sizeof(struct TagInfo));
}

int NFCReadTag()
{
    uint8_t message[5] = {0};
    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;

	readCount++;

	/*if(readCount % 10 == 0)
	{
		printf("%d Ready...\n\n", readCount);
	}*/

	//7 **
	message[0] = 0x06;  //IRQ0
	message[1] = 0x7F;//0x7F;  //Clears all bits
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x07; //IRQ1
	message[1] = 0x7F;//0xE0; //OK
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//vTaskDelay(1000 / portTICK_PERIOD_MS);
	vTaskDelay(responseDelay / portTICK_PERIOD_MS);



//		message[0] = 0x08; //IRQ
//		message[1] = 0x7F;//08;	//8=TxIRQEn
//		i2c_master_write_slave(slaveAddressNFC, message, 2);
//
//		message[0] = 0x09;
//		message[1] = 0x60;///PushPull
//		i2c_master_write_slave(slaveAddressNFC, message, 2);

	//
//		regNr = 0x06;
//		i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//		i2c_master_read_slave(slaveAddressNFC, message, 1);
//		printf("Aft reg 0x%02X: 0x%02X\n",  regNr, message[0]);
//
//		regNr = 0x07;
//		i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//		i2c_master_read_slave(slaveAddressNFC, message, 1);
//		printf("Aft reg 0x%02X: 0x%02X\n\n",  regNr, message[0]);



	//8
	message[0] = 0x2C;
	message[1] = 0x18;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//9
	message[0] = 0x2D;
	message[1] = 0x18;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//10
	message[0] = 0x2E;
	message[1] = 0x0F;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//11
	message[0] = 0x05;
	message[1] = 0x26;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//12
	message[0] = 0x00;
	message[1] = 0x07;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//Wait
	vTaskDelay(responseDelay / portTICK_PERIOD_MS);

//		regNr = 0x06;
//		i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//		i2c_master_read_slave(slaveAddressNFC, message, 1);
//		printf("1 reg 0x%02X: 0x%02X\n",  regNr, message[0]);


	uint8_t regNr = 0x06;
	i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 1);
	//printf("1 reg 0x%02X: 0x%02X\n",  regNr, message[0]);

	//Check if RxIRQ bit is set in IRQ0 register
	if(!(message[0] & (1<<2)))
		return 0;

	printf("Card detected!\n");




	//13
	uint8_t reg = 0x05;
	i2c_master_write_slave(slaveAddressNFC, &reg, 1);

	i2c_master_read_slave(slaveAddressNFC, message, 2);



	if(message[0] == 0x04)
	{
		uidLength = 4;
		printf("Single UID\n");
	}
	else if ((message[0] == 0x44) && (message[1] == 0x0))
	{
		uidLength = 7;
		printf("Double UID\n");
	}
	else
	{
		if((message[0] != 0x26) && (message[0] != 0x26))
			printf("Unknown ATQA: %02X %02X\n", message[1], message[0]);

		vTaskDelay(500 / portTICK_PERIOD_MS);
		return -1;
	}

//	    regNr = 0x06;
//	    i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//		i2c_master_read_slave(slaveAddressNFC, message, 1);
//		printf("2 reg 0x%02X: 0x%02X\n",  regNr, message[0]);

	uint8_t lenReg = 0x04;
	i2c_master_write_slave(slaveAddressNFC, &lenReg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 1);
	//printf("Len1: %02X\n",  message[0]);

//		regNr = 0x06;
//	    i2c_master_write_slave(slaveAddressNFC, &regNr, 1);
//		i2c_master_read_slave(slaveAddressNFC, message, 1);
//		printf("3 reg 0x%02X: 0x%02X\n",  regNr, message[0]);


	//UID 1

	message[0] = 0x2E;//TxDataNum
	message[1] = 0x08;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x0C;//RxBitCtrl
	message[1] = 0x00;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x00;//Command
	message[1] = 0x00;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x02;//FIFOControl
	message[1] = 0xB0;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//12
	message[0] = 0x05;
	message[1] = 0x93;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x05;
	message[1] = 0x20;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x00;
	message[1] = 0x07;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//Wait
	vTaskDelay(responseDelay / portTICK_PERIOD_MS);

	//Read length
	i2c_master_write_slave(slaveAddressNFC, &lenReg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 1);
	//printf("Len3: %02X\n",  message[0]);

	reg = 0x05;
	i2c_master_write_slave(slaveAddressNFC, &reg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 5);

	printf("Part 1: %02X %02X %02X %02X %02X - ", message[0], message[1], message[2], message[3], message[4]);

	uint8_t BBC = message[0] ^  message[1] ^  message[2] ^  message[3];

	if(BBC == message[4])
	{
		printf("Valid BBC %X == %X\n", BBC, message[4]);
	}
	else
	{
		printf("Invalid BBC %X != %X\n", BBC, message[4]);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		return -2;
	}

	if(uidLength == 4)
	{
		uid[0] = message[0];
		uid[1] = message[1];
		uid[2] = message[2];
		uid[3] = message[3];
		printf("SINGLE UID: %02X %02X %02X %02X\n", uid[0], uid[1], uid[2], uid[3] );

		tagInfo.tagIsValid = true;
		tagInfo.idLength = 4;
		memcpy(tagInfo.id, uid, tagInfo.idLength);
	}
	else if (uidLength == 7)
	{
		uid[0] = message[1];
		uid[1] = message[2];
		uid[2] = message[3];
	}
	else
	{
		printf("Unknown uidLength\n\n");
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		return -3;
	}

	if(message[0] != 0x88)
	{
		printf("\n\n");
		//vTaskDelay(2000 / portTICK_PERIOD_MS);
		return 1;
	}


	/*********** Select card sequence ***/

	vTaskDelay(responseDelay / portTICK_PERIOD_MS);

	uint8_t secondMessage[8] = {0};
	secondMessage[0] = 0x2C;//TxCrcPresent
	secondMessage[1] = 0x19;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x2D;//RxCcrPresent
	secondMessage[1] = 0x19;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x00;//Command
	secondMessage[1] = 0x00;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x02;//FIFOControl
	secondMessage[1] = 0xB0;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x05;
	secondMessage[1] = 0x93;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x05;
	secondMessage[1] = 0x70;
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 2);

	secondMessage[0] = 0x05;
	secondMessage[1] = message[0];
	secondMessage[2] = message[1];
	secondMessage[3] = message[2];
	secondMessage[4] = message[3];
	secondMessage[5] = message[4];
	i2c_master_write_slave(slaveAddressNFC, secondMessage, 6);

	message[0] = 0x00;
	message[1] = 0x07;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	vTaskDelay(responseDelay / portTICK_PERIOD_MS);

	//Read length
	uint8_t fifoLen = 0;
	i2c_master_write_slave(slaveAddressNFC, &lenReg, 1);
	i2c_master_read_slave(slaveAddressNFC, &fifoLen, 1);
	//printf("Len4: %02X\n",  fifoLen);

	//Avoid reading more than buffer
	if(fifoLen > 5)
		fifoLen = 5;

	reg = 0x05;
	i2c_master_write_slave(slaveAddressNFC, &reg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, fifoLen);


	//printf("Ret: %02X %02X %02X %02X %02X\n", message[0], message[1], message[2], message[3], message[4]);
	/*printf("Ret: ");
	for (int i=0; i<fifoLen; i++)
		printf("%02X ", message[i]);

	printf("\n");*/

	/***********/

	//UID 2

	//8
	message[0] = 0x2C;
	message[1] = 0x18;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//9
	message[0] = 0x2D;
	message[1] = 0x18;
	i2c_master_write_slave(slaveAddressNFC, message, 2);


	message[0] = 0x00;//Command
	message[1] = 0x00;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x02;//FIFOControl
	message[1] = 0xB0;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//12
	message[0] = 0x05;
	message[1] = 0x95;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x05;
	message[1] = 0x20;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	message[0] = 0x00;
	message[1] = 0x07;
	i2c_master_write_slave(slaveAddressNFC, message, 2);

	//Wait
	vTaskDelay(responseDelay / portTICK_PERIOD_MS);

	//Read length
	i2c_master_write_slave(slaveAddressNFC, &lenReg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 1);
	//printf("Len5: %02X\n",  message[0]);

	reg = 0x05;
	i2c_master_write_slave(slaveAddressNFC, &reg, 1);
	i2c_master_read_slave(slaveAddressNFC, message, 5);

	printf("Part 2: %02X %02X %02X %02X %02X - ", message[0], message[1], message[2], message[3], message[4]);

	uint8_t BBC2 = message[0] ^  message[1] ^  message[2] ^  message[3];

	if(BBC2 == message[4])
		printf("Valid BBC %X == %X\n", BBC2, message[4]);
	else
		printf("Invalid BBC %X != %X\n", BBC2, message[4]);

	uid[3] = message[0];
	uid[4] = message[1];
	uid[5] = message[2];
	uid[6] = message[3];

	printf("DOUBLE UID: ");
	for (int i = 0; i < uidLength; i++)
		printf("%02X ", uid[i]);

	printf("\n\n");

	tagInfo.tagIsValid = true;
	tagInfo.idLength = 7;
	memcpy(tagInfo.id, uid, tagInfo.idLength);

	validId = true;

	//vTaskDelay(3000 / portTICK_PERIOD_MS);

    return 2;
}

