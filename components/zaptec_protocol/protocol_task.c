#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/xtensa_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"
#include "mcu_communication.h"

#define TAG __FILE__

#define RX_TIMEOUT  (3000 / (portTICK_PERIOD_MS))

void uartCommsTask(void *pvParameters);
void uartRecvTask(void *pvParameters);
void configureUart();
void onCharRx(char c);

SemaphoreHandle_t uart_write_lock;
QueueHandle_t uart_recv_message_queue;
QueueHandle_t uart0_events_queue;
uint32_t mcuCommunicationError = 0;

const int uart_num = UART_NUM_2;

void zaptecProtocolStart(){
    ESP_LOGI(TAG, "starting protocol task");
    static uint8_t ucParameterToPass = {0};
    TaskHandle_t uartRecvTaskHandle = NULL;
    TaskHandle_t taskHandle = NULL;
    int stack_size = 8192;//4096;
    xTaskCreate( uartRecvTask, "uartRecvTask", stack_size, &ucParameterToPass, 6, &uartRecvTaskHandle );
    xTaskCreate( uartCommsTask, "UARTCommsTask", stack_size, &ucParameterToPass, 5, &taskHandle );
    configASSERT(uartRecvTaskHandle);
    configASSERT( taskHandle );
    if( taskHandle == NULL ){
        ESP_LOGE(TAG, "failed to start task");
    }
    
}

ZapMessage runRequest(const uint8_t *encodedTxBuf, uint length){

    if( xSemaphoreTake( uart_write_lock, RX_TIMEOUT ) == pdTRUE )
    {
    
    	uart_flush(uart_num);
        xQueueReset(uart_recv_message_queue);

        uart_write_bytes(uart_num, (char *)encodedTxBuf, length);

        ZapMessage rxMsg = {0};
        xQueueReceive( 
            uart_recv_message_queue,
            &( rxMsg ),
			RX_TIMEOUT//portMAX_DELAY
        );

        // dont release uart_write_lock, let caller use freeZapMessageReply()
        return rxMsg;
    }
    configASSERT(false);
    ZapMessage dummmy_reply = {0};
    return dummmy_reply;
}

void freeZapMessageReply(){
    xSemaphoreGive(uart_write_lock) ;
}

void uartRecvTask(void *pvParameters){
    uart_write_lock = xSemaphoreCreateMutex();
    configASSERT(uart_write_lock);
    uart_recv_message_queue = xQueueCreate( 1, sizeof( ZapMessage ) );
    configASSERT(uart_recv_message_queue);

    configureUart();

    ZapMessage rxMsg;
    uint8_t uart_data_size = 128;
    uint8_t uart_data[uart_data_size];

    uart_event_t event;

    while(true)
        {
            //configASSERT(xQueueReceive(uart0_events_queue, (void * )&event, (portTickType)RX_TIMEOUT))
			xQueueReceive(uart0_events_queue, (void * )&event, (portTickType)RX_TIMEOUT);

            if(event.type != UART_DATA){continue;}

            if(uxSemaphoreGetCount(uart_write_lock)==1){
                ESP_LOGE(TAG, "got uart data without outstanding request");
                continue;
            }



            configASSERT(event.size <= uart_data_size);
            int length = uart_read_bytes(uart_num, uart_data, event.size, RX_TIMEOUT);
    	//int length = uart_read_bytes(uart_num, uart_data, 1, RX_TIMEOUT);

            if((event.timeout_flag == true) && (length == 0))
       		{
            	mcuCommunicationError++;
            	continue;
       		}


            //ESP_LOGI(TAG, "feeding %d bytes to ZParseFrame:", length);

            for(int i = 0; i<length; i++){
                uint8_t rxByte = uart_data[i];

                if(ZParseFrame(rxByte, &rxMsg))
                {   
                    uart_flush(uart_num);
                    configASSERT(xQueueSend(
                        uart_recv_message_queue,                        
                        ( void * ) &rxMsg,
                        portMAX_DELAY
                    ))
                    //printf("handling frame\n\r");
                }
            }
        }
}

volatile static float temperaturePowerBoardT[2]  = {0.0};
volatile static float temperatureEmeter[3] = {0.0};
volatile static float voltages[3] = {0.0};
volatile static float currents[3] = {0.0};

float GetFloat(uint8_t * input)
{
	float tmp = 0.0;

	uint8_t swap[4] = {0};
	swap[0] = input[3];
	swap[1] = input[2];
	swap[2] = input[1];
	swap[3] = input[0];
	memcpy(&tmp, &swap[0], 4);
	return tmp;
}


void uartCommsTask(void *pvParameters){
    ESP_LOGI(TAG, "configuring uart");

    //Provide application time to initialize before sending to MCU
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    int count = 0;
    while (true)
    {
    	count++;

        // tx test
        //ESP_LOGI(TAG, "creating zap message");
        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        

        switch (count)
        {
        	case 1:
				txMsg.identifier = ParamInternalTemperatureEmeter;
				break;
        	case 2:
				txMsg.identifier = ParamInternalTemperatureEmeter2;
				break;
        	case 3:
                txMsg.identifier = ParamInternalTemperatureEmeter3;
                break;

        	case 4:
        		txMsg.identifier = ParamInternalTemperatureT;
        		break;
        	case 5:
				txMsg.identifier = ParamInternalTemperatureT2;
				break;



        	case 6:
				txMsg.identifier = ParamVoltagePhase1;
				break;
        	case 7:
				txMsg.identifier = ParamVoltagePhase2;
				break;
        	case 8:
				txMsg.identifier = ParamVoltagePhase3;
				break;
        	case 9:
				txMsg.identifier = ParamCurrentPhase1;
				break;
        	case 10:
				txMsg.identifier = ParamCurrentPhase2;
				break;
        	case 11:
				txMsg.identifier = ParamCurrentPhase3;
				break;
        	/*default:
        		vTaskDelay(1000 / portTICK_PERIOD_MS);
        		continue;
        		break;*/
        }

        if(count >= 12)
        {
        	//ESP_LOGI(TAG, "count == 12");
        	vTaskDelay(1000 / portTICK_PERIOD_MS);
        	count = 0;
        	continue;
        }

        txMsg.type = MsgRead;//MsgWrite;

        //ESP_LOGI(TAG, "before encoding");
        uint encoded_length = ZEncodeMessageHeaderOnly(
                    &txMsg, txBuf, encodedTxBuf
                );

        //ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
        
        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
        //printf("frame type: %d \n\r", rxMsg.type);
        //printf("frame identifier: %d \n\r", rxMsg.identifier);
//        printf("frame timeId: %d \n\r", rxMsg.timeId);


        /*uint8_t swap[4] = {0};
        if(rxMsg.identifier == 201)
        {
        	swap[0] = rxMsg.data[3];
        	swap[1] = rxMsg.data[2];
        	swap[2] = rxMsg.data[1];
        	swap[3] = rxMsg.data[0];
        	memcpy(&temperature5, &swap[0], 4);
        	printf("Temperature: %f C\n\r", temperature5);
        }*/

        if(rxMsg.identifier == ParamInternalTemperatureEmeter)
			temperatureEmeter[0] = GetFloat(rxMsg.data);
		else if(rxMsg.identifier == ParamInternalTemperatureEmeter2)
			temperatureEmeter[1] = GetFloat(rxMsg.data);
		else if(rxMsg.identifier == ParamInternalTemperatureEmeter3)
			temperatureEmeter[2] = GetFloat(rxMsg.data);
		else if(rxMsg.identifier == ParamInternalTemperatureT)
        	temperaturePowerBoardT[0] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamInternalTemperatureT2)
            temperaturePowerBoardT[1] = GetFloat(rxMsg.data);

        else if(rxMsg.identifier == ParamVoltagePhase1)
            voltages[0] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamVoltagePhase2)
        	voltages[1] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamVoltagePhase3)
        	voltages[2] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamCurrentPhase1)
        	currents[0] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamCurrentPhase2)
        	currents[1] = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamCurrentPhase3)
        {
        	currents[2] = GetFloat(rxMsg.data);
        	ESP_LOGW(TAG, "Dataset: T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  Timeouts: %i", temperatureEmeter[0], temperatureEmeter[1], temperatureEmeter[2], temperaturePowerBoardT[0], temperaturePowerBoardT[1], voltages[0], voltages[1], voltages[2], currents[0], currents[1], currents[2], mcuCommunicationError);
        }

        /*else if(rxMsg.type != 0)
        {
        	uint8_t error_code = ZDecodeUInt8(rxMsg.data);
        	printf("frame error code: %d\n\r", error_code);
        }*/
        freeZapMessageReply();


        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
}

float MCU_GetEmeterTemperature(uint8_t phase)
{
	return temperatureEmeter[phase];
}

float MCU_GetTemperaturePowerBoard(uint8_t sensor)
{
	return temperaturePowerBoardT[sensor];
}

float MCU_GetTemperature()
{
	return 0.0;//temperaturePowerBoardT;
}

float MCU_GetVoltages(uint8_t phase)
{
	return voltages[phase];
}

float MCU_GetCurrents(uint8_t phase)
{
	return currents[phase];
}

void configureUart(){
    int tx_pin = GPIO_NUM_26;
    int rx_pin = GPIO_NUM_25;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    const int uart_buffer_size = (1024 * 2);

    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, \
        uart_buffer_size, 20, &uart0_events_queue, 0));

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));    

    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}
