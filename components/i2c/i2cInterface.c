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
#include "freertos/FreeRTOS.h"
#include "SHT30.h"

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */

static const char *TAG = "I2C-INTERFACE";

static gpio_num_t i2c_gpio_sda = 19;
static gpio_num_t i2c_gpio_scl = 18;
static uint32_t i2c_frequency = 100000;
static i2c_port_t i2c_port = I2C_NUM_0;


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

//static struct {
//    struct arg_int *port;
//    struct arg_int *freq;
//    struct arg_int *sda;
//    struct arg_int *scl;
//    struct arg_end *end;
//} i2cconfig_args;



//#define ESP_SLAVE_ADDR 0x2A	//NFC-A
#define ESP_SLAVE_ADDR 0x2B	//NFC-B
//#define ESP_SLAVE_ADDR 0x44	//SHT-30
//#define ESP_SLAVE_ADDR 0x51		//RTC
//#define ESP_SLAVE_ADDR 0x56		//EEPROM-CAT24C04

esp_err_t i2c_master_write_slave(uint8_t slave_addr, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_master_read_slave(uint8_t slave_addr, uint8_t *data_rd, size_t size)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}


esp_err_t i2c_master_write_slave_at_address(uint8_t slave_addr, uint8_t wr_reg, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, wr_reg, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}


esp_err_t i2c_master_read_slave_at_address(uint8_t slave_addr, uint8_t rd_reg, uint8_t *data_rd, size_t size)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, rd_reg, ACK_CHECK_EN);
    //i2c_master_stop(cmd);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | READ_BIT, ACK_CHECK_EN);

    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }

    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}




//static int do_i2cdetect_cmd(int argc, char **argv)
void do_i2cdetect_cmd()
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
}






//static void register_i2cdectect(void)
//{
//    const esp_console_cmd_t i2cdetect_cmd = {
//        .command = "i2cdetect",
//        .help = "Scan I2C bus for devices",
//        .hint = NULL,
//        .func = &do_i2cdetect_cmd,
//        .argtable = NULL
//    };
//    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdetect_cmd));
//}

//static struct {
//    struct arg_int *chip_address;
//    struct arg_int *register_address;
//    struct arg_int *data_length;
//    struct arg_end *end;
//} i2cget_args;
//
//
//static struct {
//    struct arg_int *chip_address;
//    struct arg_int *register_address;
//    struct arg_int *data;
//    struct arg_end *end;
//} i2cset_args;
//
//
//static struct {
//    struct arg_int *chip_address;
//    struct arg_int *size;
//    struct arg_end *end;
//} i2cdump_args;



/*void register_i2ctools(void)
{
    //register_i2cconfig();
	do_i2cdetect_cmd();
	//SHT30ReadTemperature();
	//WriteRTC();
	//ReadRTC();
	//WriteEEPROM();
    //register_i2cdectect();
    //register_i2cget();
    //register_i2cset();
    //register_i2cdump();
}*/


//static void i2c_task(void *pvParameters)
//{
//
//	do_i2cdetect_cmd();
//	SHT30Init();
//
//	while (true)
//	{
//		SHT30ReadTemperature();
//		ESP_LOGI(TAG, "waiting for connection");
//		vTaskDelay(3000 / portTICK_RATE_MS);
//	}
//}

//void I2CDevicesInit()
//{
//	static uint8_t ucParameterToPass = {0};
//	TaskHandle_t taskHandle = NULL;
//	xTaskCreate( i2c_task, "ocppTask", 4096, &ucParameterToPass, 5, &taskHandle );
//
//}
