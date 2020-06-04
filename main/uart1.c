#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "driver/gpio.h"
#include "uart1.h"
/*#include "APM.h"
#include "production_test.h"
#include "https_client.h"*/

/*#define ECHO_TEST_TXD  (GPIO_NUM_17)
#define ECHO_TEST_RXD  (GPIO_NUM_16)*/
#define ECHO_TEST_TXD1  (GPIO_NUM_17)
#define ECHO_TEST_RXD1  (GPIO_NUM_16)
#define ECHO_TEST_RTS1  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS1  (UART_PIN_NO_CHANGE)

#define BUF_SIZE (1024)

static const char *TAG = "MBUS   :";

static int mbusReceptionCounter = 0;
static int mbusCheckCounter = 0;
static bool isReceiving = true;
static int shortCounter = 0;
static bool prodTestReceiving = false;
static bool buttonStartProductionTest = false;

int apmHandleCounter = 0;

#define ECHO_TEST_TXD0  (GPIO_NUM_1)
#define ECHO_TEST_RXD0  (GPIO_NUM_3)
#define ECHO_TEST_RTS0  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS0  (UART_PIN_NO_CHANGE)


static void mbus_task()
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD1, ECHO_TEST_RXD1, ECHO_TEST_RTS1, ECHO_TEST_CTS1);
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);


    uart_config_t uart_config0 = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config0);
    uart_set_pin(UART_NUM_0, ECHO_TEST_TXD0, ECHO_TEST_RXD0, ECHO_TEST_RTS0, ECHO_TEST_CTS0);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);



    // Configure a temporary buffer for the incoming data
    uint8_t * readData = (uint8_t *) malloc(1024);
    char * writeData = (char *) malloc(1024);

    receivedLength = 0;


    obisRawData = (unsigned char *) malloc(1024);
    bufferReady = 1;
    newData = 0;
    measurementNo = 1;

    int emptyCount = 0;
    int len = 0;
    char count = 66;

    readData[0] = 'G';
    readData[1] = 'o';
    uart_write_bytes(UART_NUM_0, (char*)readData, 2);

 	while (1) {
 		count++;
 		//*writeData = count;

   		len = uart_read_bytes(UART_NUM_0, (uint8_t*)writeData, BUF_SIZE, 20 / portTICK_RATE_MS);
   		if(len > 0)
   			uart_write_bytes(UART_NUM_1, writeData, len);

   		vTaskDelay(50/portTICK_PERIOD_MS);

   		len = uart_read_bytes(UART_NUM_1, readData, BUF_SIZE, 20 / portTICK_RATE_MS);
   		if(len > 0)
   		   	uart_write_bytes(UART_NUM_0, (char*)readData, len);

   		//uart_write_bytes(UART_NUM_0, writeData, 1);

		//if(len > 0)
			//ESP_LOGE(TAG, "Len %d, data %X", len, readData[0]);

		vTaskDelay(50/portTICK_PERIOD_MS);
	}

    writeData[0] = 'A';
    writeData[1] = 'T';
    writeData[2] = '\r';

    //AT+CIMI


    while (1) {
		count++;

		uart_write_bytes(UART_NUM_1, writeData, 3);
		vTaskDelay(1000/portTICK_PERIOD_MS);


		len = uart_read_bytes(UART_NUM_1, readData, BUF_SIZE, 20 / portTICK_RATE_MS);

		if(len > 0)
			printf("\nLength: %d: \n", len);

		for (int i = 0; i < len;i++)
			printf("%c", readData[i]);

		if (len > 0)
			printf("\n");

		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

void mbus_ButtonStartProductionTest()
{
	buttonStartProductionTest = true;
}

bool mbus_CheckReception()
{
	mbusCheckCounter++;
	if(mbusCheckCounter > 150)
	{
		if(mbusReceptionCounter > 0)
			isReceiving = true;
		else
			isReceiving = false;

		mbusReceptionCounter = 0;
		mbusCheckCounter = 0;
	}

	return isReceiving;
}

uint32_t mbus_GetMeasurementNo()
{
	return measurementNo;
}

void mbus_init(){

#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
	//xTaskCreate(mbus_task, "uart_mbus_task", 4096, NULL, 4, NULL);
	xTaskCreate(mbus_task, "uart_mbus_task", 5096, NULL, 6, NULL);
}
