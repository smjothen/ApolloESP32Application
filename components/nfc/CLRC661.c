/* cmd_i2ctools.c

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
//#include "argtable3/argtable3.h"
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_log.h"

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */

static const char *TAG = "cmd_i2ctools";

static gpio_num_t i2c_gpio_sda = 19;//18;
static gpio_num_t i2c_gpio_scl = 18;//19;
static uint32_t i2c_frequency = 100000;
static i2c_port_t i2c_port = I2C_NUM_0;

static esp_err_t i2c_get_port(int port, i2c_port_t *i2c_port)
{
    if (port >= I2C_NUM_MAX) {
        ESP_LOGE(TAG, "Wrong port number: %d", port);
        return ESP_FAIL;
    }
    switch (port) {
    case 0:
        *i2c_port = I2C_NUM_0;
        break;
    case 1:
        *i2c_port = I2C_NUM_1;
        break;
    default:
        *i2c_port = I2C_NUM_0;
        break;
    }
    return ESP_OK;
}

static esp_err_t i2c_master_driver_initialize(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_gpio_sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = i2c_gpio_scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = i2c_frequency
    };
    return i2c_param_config(i2c_port, &conf);
}

static struct {
    struct arg_int *port;
    struct arg_int *freq;
    struct arg_int *sda;
    struct arg_int *scl;
    struct arg_end *end;
} i2cconfig_args;

//static int do_i2cconfig_cmd(int argc, char **argv)
//{
//    int nerrors = arg_parse(argc, argv, (void **)&i2cconfig_args);
//    if (nerrors != 0) {
//        arg_print_errors(stderr, i2cconfig_args.end, argv[0]);
//        return 0;
//    }
//
//    /* Check "--port" option
//    if (i2cconfig_args.port->count) {
//        if (i2c_get_port(i2cconfig_args.port->ival[0], &i2c_port) != ESP_OK) {
//            return 1;
//        }
//    }
//     Check "--freq" option
//    if (i2cconfig_args.freq->count) {
//        i2c_frequency = i2cconfig_args.freq->ival[0];
//    }
//     Check "--sda" option
//    i2c_gpio_sda = i2cconfig_args.sda->ival[0];
//     Check "--scl" option
//    i2c_gpio_scl = i2cconfig_args.scl->ival[0];*/
//    return 0;
//}

//static void register_i2cconfig(void)
//{
//    i2cconfig_args.port = I2C_NUM_0;//arg_int0(NULL, "port", "<0|1>", "Set the I2C bus port number");
//    i2cconfig_args.freq = 100000;//arg_int0(NULL, "freq", "<Hz>", "Set the frequency(Hz) of I2C bus");
//    i2cconfig_args.sda = 19;//arg_int1(NULL, "sda", "<gpio>", "Set the gpio for I2C SDA");
//    i2cconfig_args.scl = 18;//arg_int1(NULL, "scl", "<gpio>", "Set the gpio for I2C SCL");
//    i2cconfig_args.end = 2;//arg_end(2);
//    const esp_console_cmd_t i2cconfig_cmd = {
//        .command = "i2cconfig",
//        .help = "Config I2C bus",
//        .hint = NULL,
//        .func = &do_i2cconfig_cmd,
//        .argtable = &i2cconfig_args
//    };
//    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
//}

//#define ESP_SLAVE_ADDR 0x2A	//NFC-A
//#define ESP_SLAVE_ADDR 0x2B	//NFC-B
//#define ESP_SLAVE_ADDR 0x44	//SHT-30
//#define ESP_SLAVE_ADDR 0x51		//RTC
#define ESP_SLAVE_ADDR 0x56		//EEPROM-CAT24C04

static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t *data_rd, size_t size)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}


static esp_err_t i2c_master_write_slave_at_address(i2c_port_t i2c_num, uint8_t wr_reg, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, wr_reg, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}


static esp_err_t i2c_master_read_slave_at_address(i2c_port_t i2c_num, uint8_t rd_reg, uint8_t *data_rd, size_t size)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, rd_reg, ACK_CHECK_EN);
    //i2c_master_stop(cmd);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | READ_BIT, ACK_CHECK_EN);

    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }

    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}




//static int do_i2cdetect_cmd(int argc, char **argv)
static int do_i2cdetect_cmd()
{
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_master_driver_initialize();
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
    printf("I2C active\n");


//    1: writeRegister(0x00, 0x00);
//    2: writeRegister(0x02, 0xB0);
//    3: writeRegister(0x05, 0x00, 0x00);
//    4: writeRegister(0x00, 0x0D);
//    5: writeRegister(0x02, 0xB0);
//    6: writeRegister(0x28, 0x8E);
//    7: writeRegister(0x06, 0x7F);
//    8: writeRegister(0x2C, 0x18);
//    9: writeRegister(0x2D, 0x18);
//    10: writeRegister(0x2E, 0x0F);
//    11: writeRegister(0x05, 0x26);
//    12: writeRegister(0x00, 0x07);
//    13: waitForCardResponse();
//    14: readRegister(0x05, 0x05, 0x00);


    uint8_t reg = 0x7F;
    i2c_master_write_slave(I2C_NUM_0, &reg, 1);

    volatile uint8_t message[5] = {0};
    volatile uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    i2c_master_read_slave(I2C_NUM_0, message, 1);

    //uint responseDelay = 100;
    uint responseDelay = 10;

    message[0] = 0x08; //IRQ0En
	message[1] = 0x84;//0x84;//0x8C;//08;	//8=TxIRQEn
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	message[0] = 0x09; //IRQ1En
	message[1] = 0x60;//OpenDrain  alt: 0xE0;///PushPull also works
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	uint8_t regNr = 0x08;
//	i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//	i2c_master_read_slave(I2C_NUM_0, message, 1);
//	printf("reg 0x%02X: 0x%02X\n",  regNr, message[0]);
//
//	regNr = 0x09;
//	i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//	i2c_master_read_slave(I2C_NUM_0, message, 1);
//	printf("reg 0x%02X: 0x%02X\n\n",  regNr, message[0]);

	//1
	message[0] = 0x0;
	message[1] = 0x0;
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	//2
	message[0] = 0x02;
	message[1] = 0xB0;
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	//3
	message[0] = 0x05;
	message[1] = 0x00;
	message[2] = 0x00;
	i2c_master_write_slave(I2C_NUM_0, message, 3);

	//4
	message[0] = 0x00;
	message[1] = 0x0D;
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	//5
	message[0] = 0x02;
	message[1] = 0xB0;
	i2c_master_write_slave(I2C_NUM_0, message, 2);

	//6
	message[0] = 0x28;
	message[1] = 0x8E;
	i2c_master_write_slave(I2C_NUM_0, message, 2);

    unsigned int readCount = 0;
    while(true)
	{
    	readCount++;

    	if(readCount % 10 == 0)
    	{
    		printf("%d Ready...\n\n", readCount);
    	}



//		regNr = 0x06;
//		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("Bef reg 0x%02X: 0x%02X\n",  regNr, message[0]);
//
//		regNr = 0x07;
//		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("Bef reg 0x%02X: 0x%02X\n",  regNr, message[0]);



		//7 **
		message[0] = 0x06;  //IRQ0
		message[1] = 0x7F;//0x7F;  //Clears all bits
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x07; //IRQ1
		message[1] = 0x7F;//0xE0; //OK
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		vTaskDelay(1000 / portTICK_PERIOD_MS);



//		message[0] = 0x08; //IRQ
//		message[1] = 0x7F;//08;	//8=TxIRQEn
//		i2c_master_write_slave(I2C_NUM_0, message, 2);
//
//		message[0] = 0x09;
//		message[1] = 0x60;///PushPull
//		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//
//		regNr = 0x06;
//		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("Aft reg 0x%02X: 0x%02X\n",  regNr, message[0]);
//
//		regNr = 0x07;
//		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("Aft reg 0x%02X: 0x%02X\n\n",  regNr, message[0]);



		//8
		message[0] = 0x2C;
		message[1] = 0x18;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//9
		message[0] = 0x2D;
		message[1] = 0x18;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//10
		message[0] = 0x2E;
		message[1] = 0x0F;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//11
		message[0] = 0x05;
		message[1] = 0x26;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//12
		message[0] = 0x00;
		message[1] = 0x07;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//Wait
		vTaskDelay(responseDelay / portTICK_PERIOD_MS);

//		regNr = 0x06;
//		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("1 reg 0x%02X: 0x%02X\n",  regNr, message[0]);


		bool cardDetected = false;

		regNr = 0x06;
		i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 1);
		//printf("1 reg 0x%02X: 0x%02X\n",  regNr, message[0]);

		//Check if RxIRQ bit is set in IRQ0 register
		if(message[0] & (1<<2))
			cardDetected = true;
		else
			continue;

		printf("Card detected!\n");




		//13
	    uint8_t reg = 0x05;
	    i2c_master_write_slave(I2C_NUM_0, &reg, 1);

	    i2c_master_read_slave(I2C_NUM_0, message, 2);



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
	    	continue;
	    }

//	    regNr = 0x06;
//	    i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("2 reg 0x%02X: 0x%02X\n",  regNr, message[0]);

	    uint8_t lenReg = 0x04;
		i2c_master_write_slave(I2C_NUM_0, &lenReg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 1);
		//printf("Len1: %02X\n",  message[0]);

//		regNr = 0x06;
//	    i2c_master_write_slave(I2C_NUM_0, &regNr, 1);
//		i2c_master_read_slave(I2C_NUM_0, message, 1);
//		printf("3 reg 0x%02X: 0x%02X\n",  regNr, message[0]);


		//UID 1

		message[0] = 0x2E;//TxDataNum
		message[1] = 0x08;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x0C;//RxBitCtrl
		message[1] = 0x00;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x00;//Command
		message[1] = 0x00;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x02;//FIFOControl
		message[1] = 0xB0;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//12
		message[0] = 0x05;
		message[1] = 0x93;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x05;
		message[1] = 0x20;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x00;
		message[1] = 0x07;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//Wait
		vTaskDelay(responseDelay / portTICK_PERIOD_MS);

		//Read length
		i2c_master_write_slave(I2C_NUM_0, &lenReg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 1);
		//printf("Len3: %02X\n",  message[0]);

		reg = 0x05;
		i2c_master_write_slave(I2C_NUM_0, &reg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 5);

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
			continue;
		}

		if(uidLength == 4)
		{
			uid[0] = message[0];
			uid[1] = message[1];
			uid[2] = message[2];
			uid[3] = message[3];
			printf("SINGLE UID: %02X %02X %02X %02X\n", uid[0], uid[1], uid[2], uid[3] );
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
			continue;
		}

		if(message[0] != 0x88)
		{
			printf("\n\n");
			vTaskDelay(2000 / portTICK_PERIOD_MS);
			continue;
		}


		/*********** Select card sequence ***/

		vTaskDelay(responseDelay / portTICK_PERIOD_MS);

		uint8_t secondMessage[8] = {0};
		secondMessage[0] = 0x2C;//TxCrcPresent
		secondMessage[1] = 0x19;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x2D;//RxCcrPresent
		secondMessage[1] = 0x19;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x00;//Command
		secondMessage[1] = 0x00;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x02;//FIFOControl
		secondMessage[1] = 0xB0;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x05;
		secondMessage[1] = 0x93;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x05;
		secondMessage[1] = 0x70;
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 2);

		secondMessage[0] = 0x05;
		secondMessage[1] = message[0];
		secondMessage[2] = message[1];
		secondMessage[3] = message[2];
		secondMessage[4] = message[3];
		secondMessage[5] = message[4];
		i2c_master_write_slave(I2C_NUM_0, secondMessage, 6);

		message[0] = 0x00;
		message[1] = 0x07;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		vTaskDelay(responseDelay / portTICK_PERIOD_MS);

		//Read length
		uint8_t fifoLen = 0;
		i2c_master_write_slave(I2C_NUM_0, &lenReg, 1);
		i2c_master_read_slave(I2C_NUM_0, &fifoLen, 1);
		//printf("Len4: %02X\n",  fifoLen);

		//Avoid reading more than buffer
		if(fifoLen > 5)
			fifoLen = 5;

		reg = 0x05;
		i2c_master_write_slave(I2C_NUM_0, &reg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, fifoLen);


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
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//9
		message[0] = 0x2D;
		message[1] = 0x18;
		i2c_master_write_slave(I2C_NUM_0, message, 2);


		message[0] = 0x00;//Command
		message[1] = 0x00;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x02;//FIFOControl
		message[1] = 0xB0;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//12
		message[0] = 0x05;
		message[1] = 0x95;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x05;
		message[1] = 0x20;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		message[0] = 0x00;
		message[1] = 0x07;
		i2c_master_write_slave(I2C_NUM_0, message, 2);

		//Wait
		vTaskDelay(responseDelay / portTICK_PERIOD_MS);

		//Read length
		i2c_master_write_slave(I2C_NUM_0, &lenReg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 1);
		//printf("Len5: %02X\n",  message[0]);

		reg = 0x05;
		i2c_master_write_slave(I2C_NUM_0, &reg, 1);
		i2c_master_read_slave(I2C_NUM_0, message, 5);

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

		vTaskDelay(3000 / portTICK_PERIOD_MS);
	}

    //Never stop i2c
	//i2c_driver_delete(i2c_port);

    return 0;
}


// SHT30 sensor ID
#define SHT30_TH_SLAVE_ADDRESS 0x88

/* SHT30 internal registers  */
#define SHT30_SAMPLING_FREQUENCY        0x2236//0x2126
#define SHT30_PERIODIC_MEASUREMENT_2HZ  0xE000
#define SHT30_READ_STATUS_REGISTER      0xF32D

static int SHT30ReadTemperature()
{
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_master_driver_initialize();
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
    printf("I2C active\n");



    uint8_t readBytes[6] = {0};
    uint8_t writeBytes[2] = {0};
    writeBytes[0] = 0x22;
    writeBytes[1] = 0x36;
    i2c_master_write_slave(I2C_NUM_0, &writeBytes, 2);

    //rawMeasurement = ReadWordDirect(SHT30_SAMPLING_FREQUENCY, SHT30_TH_SLAVE_ADDRESS, 0);

    //uint responseDelay = 100;
    uint responseDelay = 10;
    float internalTemperature = 0.0;
    float internalHumidity = 0.0;
    unsigned int rawTemperature = 0;
    unsigned int rawHumidity = 0;

    unsigned int readCount = 0;
    while(true)
	{
    	i2c_master_read_slave(I2C_NUM_0, readBytes, 6);
    	//rawMeasurement = ReadWordDirect(SHT30_PERIODIC_MEASUREMENT_2HZ, SHT30_TH_SLAVE_ADDRESS, 1);
    	rawTemperature = (readBytes[0] << 8) + readBytes[1];
    	internalTemperature = -45 + (175 * (rawTemperature/65535.0));

    	    //if(initialized == 0)
    	    //    internalTemperature = -100.0;

    	rawHumidity = (readBytes[3] << 8) + readBytes[4];
    	internalHumidity = 100 * rawHumidity / 65535.0;

    	printf("Temperature: %f, Humidity %f\n", internalTemperature, internalHumidity);

    	vTaskDelay(1000 / portTICK_PERIOD_MS);

	}

    //Never stop i2c
	//i2c_driver_delete(i2c_port);

    return 0;
}



int  DecToBcd(int dec)
{
   return (((dec/10) << 4) | (dec % 10));
}

int BcdToDec(int bcd)
{
   return (((bcd>>4)*10) + (bcd & 0xF));
}

// SHT30 sensor ID
#define SHT30_TH_SLAVE_ADDRESS 0x88

/* SHT30 internal registers  */
#define SHT30_SAMPLING_FREQUENCY        0x2236//0x2126
#define SHT30_PERIODIC_MEASUREMENT_2HZ  0xE000
#define SHT30_READ_STATUS_REGISTER      0xF32D

static int WriteRTC()
{

	 i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
	    i2c_master_driver_initialize();
	    uint8_t address;
	    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
	    for (int i = 0; i < 128; i += 16) {
	        printf("%02x: ", i);
	        for (int j = 0; j < 16; j++) {
	            fflush(stdout);
	            address = i + j;
	            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	            i2c_master_start(cmd);
	            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
	            i2c_master_stop(cmd);
	            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
	            i2c_cmd_link_delete(cmd);
	            if (ret == ESP_OK) {
	                printf("%02x ", address);
	            } else if (ret == ESP_ERR_TIMEOUT) {
	                printf("UU ");
	            } else {
	                printf("-- ");
	            }
	        }
	        printf("\r\n");
	    }
	    printf("I2C active\n");



	    uint8_t readBytes[7] = {0};

	    uint8_t writeBytes[8] = {0};



	    writeBytes[0] = 0x00;
	    writeBytes[1] = 0x00;
	    i2c_master_write_slave(I2C_NUM_0, &writeBytes, 2);


	    uint8_t isAmPmFormat = 0;

		uint8_t seconds = 40;
		uint8_t minutes = 59;
		uint8_t hours = 23;
		uint8_t day = 28;
		uint8_t weekday = 0;
		uint8_t month = 02;
		uint8_t year = 21;


		//0x00 - CONFIG 0
		writeBytes[0] = 0x04; //Adr

		//0x04 - SECONDS
		writeBytes[1] = DecToBcd(seconds);//(seconds/10 << 4) & (seconds % 10);

		//0x05 - MINUTES
		writeBytes[2] = DecToBcd(minutes);//(minutes/10 << 4) & (minutes % 10);

		//0x06 - HOURS(Bit 5: 0 = 24h, 1 = 12h mode)
		writeBytes[3] = DecToBcd(hours);//(hours/10 << 4) & (hours % 10);

		//0x07 - DAYS
		writeBytes[4] = DecToBcd(day);

		//0x08 - WEEKDAY - NOT USED
		writeBytes[5] = 0x0;

		//0x09 - MONTH
		writeBytes[6] = DecToBcd(month);

		//0x0A - YEAR
		writeBytes[7] = DecToBcd(year);

		i2c_master_write_slave(I2C_NUM_0, &writeBytes, 8);

	    //rawMeasurement = ReadWordDirect(SHT30_SAMPLING_FREQUENCY, SHT30_TH_SLAVE_ADDRESS, 0);

	    //uint responseDelay = 100;
	    uint responseDelay = 10;
	    float internalTemperature = 0.0;
	    float internalHumidity = 0.0;
	    unsigned int rawTemperature = 0;
	    unsigned int rawHumidity = 0;

	    unsigned int readCount = 0;
	    while(true)
		{
	    	readBytes[0]=0x00;
	    	readBytes[1]=0x00;
	    	uint8_t readreg = 4;
	    	i2c_master_read_slave_at_address(I2C_NUM_0, readreg, readBytes, 7);
	    	//i2c_master_read_slave(I2C_NUM_0, readBytes, 2);
	    	//printf("Reg: 0x%02X\n", readBytes[0]);

	    	//for (int i = 0; i < 7; i++)
	    	//	printf("0x%02X\n ", readBytes[i]);
	    	printf("%02d-%02d-%02d %02d:%02d:%02d   (%02d)\n", BcdToDec(readBytes[6]), BcdToDec(readBytes[5]), BcdToDec(readBytes[3]), BcdToDec(readBytes[2]), BcdToDec(readBytes[1]), BcdToDec(readBytes[0]), BcdToDec(readBytes[4]));

//	    	printf(" Sec 0x%02X  %02d\n", readBytes[0], BcdToDec(readBytes[0]));
//	    	printf(" Sec 0x%02X  %02d\n", readBytes[0], BcdToDec(readBytes[0]));
//	    	printf(" Min 0x%02X  %02d\n", readBytes[1], BcdToDec(readBytes[1]));
//	    	printf(" Hr  0x%02X  %02d\n", readBytes[2], BcdToDec(readBytes[2]));
//	    	printf(" Day 0x%02X  %02d\n", readBytes[3], BcdToDec(readBytes[3]));
//	    	printf(" Wdy 0x%02X  %02d\n", readBytes[4], BcdToDec(readBytes[4]));
//	    	printf(" Mth 0x%02X  %02d\n", readBytes[5], BcdToDec(readBytes[5]));
//	    	printf(" Yr  0x%02X  %02d\n", readBytes[6], BcdToDec(readBytes[6]));
	    	//printf("\n");

	    	//printf("0x%02X\n ", readBytes[i]);

	    	//printf("Reg: 0x%02X\n", readBytes[0]);
	    	//rawMeasurement = ReadWordDirect(SHT30_PERIODIC_MEASUREMENT_2HZ, SHT30_TH_SLAVE_ADDRESS, 1);
	    	//rawTemperature = (readBytes[0] << 8) + readBytes[1];
	    	//internalTemperature = -45 + (175 * (rawTemperature/65535.0));

	    	    //if(initialized == 0)
	    	    //    internalTemperature = -100.0;

	    	//rawHumidity = (readBytes[3] << 8) + readBytes[4];
	    	//internalHumidity = 100 * rawHumidity / 65535.0;

	    	//printf("Temperature: %f, Humidity %f\n", internalTemperature, internalHumidity);

	    	vTaskDelay(1000 / portTICK_PERIOD_MS);

		}

	    //Never stop i2c
		//i2c_driver_delete(i2c_port);

	    return 0;
}





static int WriteEEPROM()
{

	 i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
	    i2c_master_driver_initialize();
	    uint8_t address;
	    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
	    for (int i = 0; i < 128; i += 16) {
	        printf("%02x: ", i);
	        for (int j = 0; j < 16; j++) {
	            fflush(stdout);
	            address = i + j;
	            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	            i2c_master_start(cmd);
	            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
	            i2c_master_stop(cmd);
	            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
	            i2c_cmd_link_delete(cmd);
	            if (ret == ESP_OK) {
	                printf("%02x ", address);
	            } else if (ret == ESP_ERR_TIMEOUT) {
	                printf("UU ");
	            } else {
	                printf("-- ");
	            }
	        }
	        printf("\r\n");
	    }
	    printf("I2C active\n");



	    uint8_t readBytes[16] = {0};

	    uint8_t writeBytes[16] = {0};

	    //READ
	    uint8_t readAddr = 0;
	    for (uint8_t line = 0; line <= 31; line++)
		{
	    	printf("#%02d:  ", line);

	    	i2c_master_read_slave_at_address(I2C_NUM_0, readAddr, readBytes, 16);
			for (int i = 0; i <= 15; i++)
				printf(" %03d", readBytes[i]);

			printf("\n");
			readAddr += 16;
		}

		printf("\n");




//	    for (uint8_t addr = 0; addr < 255; addr++)
//	    	writeBytes[addr]=addr;
//
//	    readBytes[0]=0x00;
//		readBytes[1]=0x00;
//		uint8_t readreg = 0;
//		i2c_master_read_slave_at_address(I2C_NUM_0, readreg, readBytes, 2);
//		for (int i = 0; i < 2; i++)
//			printf("Read:  0x%02X\n ", readBytes[i]);


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
	    	i2c_master_write_slave_at_address(I2C_NUM_0, startAddr, &writeBytes, 16);
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

//	    	readBytes[0]=0x00;
//	    	readBytes[1]=0x00;
//	    	uint8_t readreg = 0;
//	    	i2c_master_read_slave_at_address(I2C_NUM_0, readreg, readBytes, 7);
//	    	//i2c_master_read_slave(I2C_NUM_0, readBytes, 2);
//	    	//printf("Reg: 0x%02X\n", readBytes[0]);
//
//	    	for (int i = 0; i < 2; i++)
//	    		printf("Read:  0x%02X\n ", readBytes[i]);

	    	//printf("\n");

	    	//printf("0x%02X\n ", readBytes[i]);


	    	vTaskDelay(10000 / portTICK_PERIOD_MS);

		}

	    //Never stop i2c
		//i2c_driver_delete(i2c_port);

	    return 0;
}





/*
 * Information for this driver was pulled from the following datasheets.
 *
 *  http://www.nxp.com/documents/data_sheet/PCF85063A.pdf
 *  http://www.nxp.com/documents/data_sheet/PCF85063TP.pdf
 *
 *  PCF85063A -- Rev. 6 — 18 November 2015
 *  PCF85063TP -- Rev. 4 — 6 May 2015
 *
 *  https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-8263-C7_App-Manual.pdf
 *  RV8263 -- Rev. 1.0 — January 2019
 */

#define PCF85063_REG_CTRL1		0x00 /* status */
#define PCF85063_REG_CTRL1_CAP_SEL	BIT(0)
#define PCF85063_REG_CTRL1_STOP		BIT(5)

#define PCF85063_REG_CTRL2		0x01
#define PCF85063_CTRL2_AF		BIT(6)
#define PCF85063_CTRL2_AIE		BIT(7)

#define PCF85063_REG_OFFSET		0x02
#define PCF85063_OFFSET_SIGN_BIT	6	/* 2's complement sign bit */
#define PCF85063_OFFSET_MODE		BIT(7)
#define PCF85063_OFFSET_STEP0		4340
#define PCF85063_OFFSET_STEP1		4069

#define PCF85063_REG_CLKO_F_MASK	0x07 /* frequency mask */
#define PCF85063_REG_CLKO_F_32768HZ	0x00
#define PCF85063_REG_CLKO_F_OFF		0x07

#define PCF85063_REG_RAM		0x03

#define PCF85063_REG_SC			0x04 /* datetime */
#define PCF85063_REG_SC_OS		0x80

#define PCF85063_REG_ALM_S		0x0b
#define PCF85063_AEN			BIT(7)

//struct pcf85063_config {
//	struct regmap_config regmap;
//	unsigned has_alarms:1;
//	unsigned force_cap_7000:1;
//};
//
//struct pcf85063 {
//	struct rtc_device	*rtc;
//	struct regmap		*regmap;
//#ifdef CONFIG_COMMON_CLK
//	struct clk_hw		clkout_hw;
//#endif
//};
//
//static int pcf85063_rtc_read_time(struct device *dev, struct rtc_time *tm)
//{
//	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
//	int rc;
//	u8 regs[7];
//
//	/*
//	 * while reading, the time/date registers are blocked and not updated
//	 * anymore until the access is finished. To not lose a second
//	 * event, the access must be finished within one second. So, read all
//	 * time/date registers in one turn.
//	 */
//	rc = regmap_bulk_read(pcf85063->regmap, PCF85063_REG_SC, regs,
//			      sizeof(regs));
//	if (rc)
//		return rc;
//
//	/* if the clock has lost its power it makes no sense to use its time */
//	if (regs[0] & PCF85063_REG_SC_OS) {
//		dev_warn(&pcf85063->rtc->dev, "Power loss detected, invalid time\n");
//		return -EINVAL;
//	}
//
//	tm->tm_sec = bcd2bin(regs[0] & 0x7F);
//	tm->tm_min = bcd2bin(regs[1] & 0x7F);
//	tm->tm_hour = bcd2bin(regs[2] & 0x3F); /* rtc hr 0-23 */
//	tm->tm_mday = bcd2bin(regs[3] & 0x3F);
//	tm->tm_wday = regs[4] & 0x07;
//	tm->tm_mon = bcd2bin(regs[5] & 0x1F) - 1; /* rtc mn 1-12 */
//	tm->tm_year = bcd2bin(regs[6]);
//	tm->tm_year += 100;
//
//	return 0;
//}
//
//
//static int pcf85063_rtc_set_time(struct device *dev, struct rtc_time *tm)
//{
//	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
//	int rc;
//	u8 regs[7];
//
//	/*
//	 * to accurately set the time, reset the divider chain and keep it in
//	 * reset state until all time/date registers are written
//	 */
//	rc = regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL1,
//				PCF85063_REG_CTRL1_STOP,
//				PCF85063_REG_CTRL1_STOP);
//	if (rc)
//		return rc;
//
//	/* hours, minutes and seconds */
//	regs[0] = bin2bcd(tm->tm_sec) & 0x7F; /* clear OS flag */
//
//	regs[1] = bin2bcd(tm->tm_min);
//	regs[2] = bin2bcd(tm->tm_hour);
//
//	/* Day of month, 1 - 31 */
//	regs[3] = bin2bcd(tm->tm_mday);
//
//	/* Day, 0 - 6 */
//	regs[4] = tm->tm_wday & 0x07;
//
//	/* month, 1 - 12 */
//	regs[5] = bin2bcd(tm->tm_mon + 1);
//
//	/* year and century */
//	regs[6] = bin2bcd(tm->tm_year - 100);
//
//	/* write all registers at once */
//	rc = regmap_bulk_write(pcf85063->regmap, PCF85063_REG_SC,
//			       regs, sizeof(regs));
//	if (rc)
//		return rc;
//
//	/*
//	 * Write the control register as a separate action since the size of
//	 * the register space is different between the PCF85063TP and
//	 * PCF85063A devices.  The rollover point can not be used.
//	 */
//	return regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL1,
//				  PCF85063_REG_CTRL1_STOP, 0);
//}











static void register_i2cdectect(void)
{
    const esp_console_cmd_t i2cdetect_cmd = {
        .command = "i2cdetect",
        .help = "Scan I2C bus for devices",
        .hint = NULL,
        .func = &do_i2cdetect_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdetect_cmd));
}

static struct {
    struct arg_int *chip_address;
    struct arg_int *register_address;
    struct arg_int *data_length;
    struct arg_end *end;
} i2cget_args;

//static int do_i2cget_cmd(int argc, char **argv)
//{
//    int nerrors = arg_parse(argc, argv, (void **)&i2cget_args);
//    if (nerrors != 0) {
//        arg_print_errors(stderr, i2cget_args.end, argv[0]);
//        return 0;
//    }
//
//    /* Check chip address: "-c" option */
//    int chip_addr = i2cget_args.chip_address->ival[0];
//    /* Check register address: "-r" option */
//    int data_addr = -1;
//    if (i2cget_args.register_address->count) {
//        data_addr = i2cget_args.register_address->ival[0];
//    }
//    /* Check data length: "-l" option */
//    int len = 1;
//    if (i2cget_args.data_length->count) {
//        len = i2cget_args.data_length->ival[0];
//    }
//    uint8_t *data = malloc(len);
//
//    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
//    i2c_master_driver_initialize();
//    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//    i2c_master_start(cmd);
//    if (data_addr != -1) {
//        i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
//        i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
//        i2c_master_start(cmd);
//    }
//    i2c_master_write_byte(cmd, chip_addr << 1 | READ_BIT, ACK_CHECK_EN);
//    if (len > 1) {
//        i2c_master_read(cmd, data, len - 1, ACK_VAL);
//    }
//    i2c_master_read_byte(cmd, data + len - 1, NACK_VAL);
//    i2c_master_stop(cmd);
//    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
//    i2c_cmd_link_delete(cmd);
//    if (ret == ESP_OK) {
//        for (int i = 0; i < len; i++) {
//            printf("0x%02x ", data[i]);
//            if ((i + 1) % 16 == 0) {
//                printf("\r\n");
//            }
//        }
//        if (len % 16) {
//            printf("\r\n");
//        }
//    } else if (ret == ESP_ERR_TIMEOUT) {
//        ESP_LOGW(TAG, "Bus is busy");
//    } else {
//        ESP_LOGW(TAG, "Read failed");
//    }
//    free(data);
//    i2c_driver_delete(i2c_port);
//    return 0;
//}

//static void register_i2cget(void)
//{
//    i2cget_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
//    i2cget_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
//    i2cget_args.data_length = arg_int0("l", "length", "<length>", "Specify the length to read from that data address");
//    i2cget_args.end = arg_end(1);
//    const esp_console_cmd_t i2cget_cmd = {
//        .command = "i2cget",
//        .help = "Read registers visible through the I2C bus",
//        .hint = NULL,
//        .func = &do_i2cget_cmd,
//        .argtable = &i2cget_args
//    };
//    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cget_cmd));
//}

static struct {
    struct arg_int *chip_address;
    struct arg_int *register_address;
    struct arg_int *data;
    struct arg_end *end;
} i2cset_args;

//static int do_i2cset_cmd(int argc, char **argv)
//{
//    int nerrors = arg_parse(argc, argv, (void **)&i2cset_args);
//    if (nerrors != 0) {
//        arg_print_errors(stderr, i2cset_args.end, argv[0]);
//        return 0;
//    }
//
//    /* Check chip address: "-c" option */
//    int chip_addr = i2cset_args.chip_address->ival[0];
//    /* Check register address: "-r" option */
//    int data_addr = 0;
//    if (i2cset_args.register_address->count) {
//        data_addr = i2cset_args.register_address->ival[0];
//    }
//    /* Check data: "-d" option */
//    int len = i2cset_args.data->count;
//
//    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
//    i2c_master_driver_initialize();
//    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//    i2c_master_start(cmd);
//    i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
//    if (i2cset_args.register_address->count) {
//        i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
//    }
//    for (int i = 0; i < len; i++) {
//        i2c_master_write_byte(cmd, i2cset_args.data->ival[i], ACK_CHECK_EN);
//    }
//    i2c_master_stop(cmd);
//    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
//    i2c_cmd_link_delete(cmd);
//    if (ret == ESP_OK) {
//        ESP_LOGI(TAG, "Write OK");
//    } else if (ret == ESP_ERR_TIMEOUT) {
//        ESP_LOGW(TAG, "Bus is busy");
//    } else {
//        ESP_LOGW(TAG, "Write Failed");
//    }
//    i2c_driver_delete(i2c_port);
//    return 0;
//}
//
//static void register_i2cset(void)
//{
//    i2cset_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
//    i2cset_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
//    i2cset_args.data = arg_intn(NULL, NULL, "<data>", 0, 256, "Specify the data to write to that data address");
//    i2cset_args.end = arg_end(2);
//    const esp_console_cmd_t i2cset_cmd = {
//        .command = "i2cset",
//        .help = "Set registers visible through the I2C bus",
//        .hint = NULL,
//        .func = &do_i2cset_cmd,
//        .argtable = &i2cset_args
//    };
//    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cset_cmd));
//}

static struct {
    struct arg_int *chip_address;
    struct arg_int *size;
    struct arg_end *end;
} i2cdump_args;

//static int do_i2cdump_cmd(int argc, char **argv)
//{
//    int nerrors = arg_parse(argc, argv, (void **)&i2cdump_args);
//    if (nerrors != 0) {
//        arg_print_errors(stderr, i2cdump_args.end, argv[0]);
//        return 0;
//    }
//
//    /* Check chip address: "-c" option */
//    int chip_addr = i2cdump_args.chip_address->ival[0];
//    /* Check read size: "-s" option */
//    int size = 1;
//    if (i2cdump_args.size->count) {
//        size = i2cdump_args.size->ival[0];
//    }
//    if (size != 1 && size != 2 && size != 4) {
//        ESP_LOGE(TAG, "Wrong read size. Only support 1,2,4");
//        return 1;
//    }
//    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
//    i2c_master_driver_initialize();
//    uint8_t data_addr;
//    uint8_t data[4];
//    int32_t block[16];
//    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f"
//           "    0123456789abcdef\r\n");
//    for (int i = 0; i < 128; i += 16) {
//        printf("%02x: ", i);
//        for (int j = 0; j < 16; j += size) {
//            fflush(stdout);
//            data_addr = i + j;
//            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//            i2c_master_start(cmd);
//            i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
//            i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
//            i2c_master_start(cmd);
//            i2c_master_write_byte(cmd, chip_addr << 1 | READ_BIT, ACK_CHECK_EN);
//            if (size > 1) {
//                i2c_master_read(cmd, data, size - 1, ACK_VAL);
//            }
//            i2c_master_read_byte(cmd, data + size - 1, NACK_VAL);
//            i2c_master_stop(cmd);
//            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
//            i2c_cmd_link_delete(cmd);
//            if (ret == ESP_OK) {
//                for (int k = 0; k < size; k++) {
//                    printf("%02x ", data[k]);
//                    block[j + k] = data[k];
//                }
//            } else {
//                for (int k = 0; k < size; k++) {
//                    printf("XX ");
//                    block[j + k] = -1;
//                }
//            }
//        }
//        printf("   ");
//        for (int k = 0; k < 16; k++) {
//            if (block[k] < 0) {
//                printf("X");
//            }
//            if ((block[k] & 0xff) == 0x00 || (block[k] & 0xff) == 0xff) {
//                printf(".");
//            } else if ((block[k] & 0xff) < 32 || (block[k] & 0xff) >= 127) {
//                printf("?");
//            } else {
//                printf("%c", block[k] & 0xff);
//            }
//        }
//        printf("\r\n");
//    }
//    i2c_driver_delete(i2c_port);
//    return 0;
//}
//
//static void register_i2cdump(void)
//{
//    i2cdump_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
//    i2cdump_args.size = arg_int0("s", "size", "<size>", "Specify the size of each read");
//    i2cdump_args.end = arg_end(1);
//    const esp_console_cmd_t i2cdump_cmd = {
//        .command = "i2cdump",
//        .help = "Examine registers visible through the I2C bus",
//        .hint = NULL,
//        .func = &do_i2cdump_cmd,
//        .argtable = &i2cdump_args
//    };
//    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdump_cmd));
//}

void register_i2ctools(void)
{
    //register_i2cconfig();
	//do_i2cdetect_cmd();
	//SHT30ReadTemperature();
	//WriteRTC();
	//ReadRTC();
	WriteEEPROM();
    //register_i2cdectect();
    //register_i2cget();
    //register_i2cset();
    //register_i2cdump();
}
