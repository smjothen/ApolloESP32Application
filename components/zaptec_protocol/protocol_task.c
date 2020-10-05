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
uint8_t receivedSwitchState = 0;
uint8_t previousSwitchState = 0xFF;

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
    //configASSERT(false);
    ZapMessage dummmy_reply = {0};
    return dummmy_reply;
}

void freeZapMessageReply(){
    xSemaphoreGive(uart_write_lock) ;
}

void uartRecvTask(void *pvParameters){
    uart_write_lock = xSemaphoreCreateMutex();
    configASSERT(uart_write_lock);
    uart_recv_message_queue = xQueueCreate( 1, sizeof( ZapMessage ));
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
                //ESP_LOGE(TAG, "got uart data without outstanding request");
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

volatile static float totalChargePower = 0.0;
volatile static float totalChargePowerSession = 0.0;

volatile static uint8_t chargeMode = 0;
volatile static uint8_t chargeOperationMode = 0;

volatile static uint32_t mcuDebugCounter = 0;
volatile static uint32_t mcuWarnings = 0;
volatile static uint8_t mcuResetSource = 0;

static float mcuMaxInstallationCurrentSwitch = 20.0;//TODO set to 0;

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


float GetUint32_t(uint8_t * input)
{
	uint32_t tmp = 0;

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
    	//count++;

        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        


        switch (count)
        {
        	case 0:
        		txMsg.identifier = SwitchPosition;
        		break;
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

        	case 12:
				txMsg.identifier = ParamTotalChargePower;
				break;
        	case 13:
				txMsg.identifier = ParamTotalChargePowerSession;
				break;

        	case 14:
				txMsg.identifier = ParamChargeMode;
				break;
			case 15:
				txMsg.identifier = ParamChargeOperationMode;
				break;
			case 16:
				txMsg.identifier = DebugCounter;
				break;
			case 17:
				txMsg.identifier = ParamResetSource;
				break;
			case 18:
				txMsg.identifier = ParamWarnings;
				break;

        	/*default:
        		vTaskDelay(1000 / portTICK_PERIOD_MS);
        		continue;
        		break;*/
        }

        count++;

        if(count >= 19)
        //if(count >= 2)
        {
        	vTaskDelay(5000 / portTICK_PERIOD_MS);
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


        if(rxMsg.identifier == SwitchPosition)
        {
        	receivedSwitchState = rxMsg.data[0];

        	ESP_LOGW(TAG, "**** Switch read: %d ****", receivedSwitchState);

        	if(previousSwitchState != 0xff)
        	{
        		if(receivedSwitchState != previousSwitchState)
        		{
        			ESP_LOGW(TAG, "**** Switch reset ****");
        			esp_restart();
        		}
        	}
        	previousSwitchState = receivedSwitchState;
        }
        else if(rxMsg.identifier == ParamInternalTemperatureEmeter)
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
        	currents[2] = GetFloat(rxMsg.data);

        else if(rxMsg.identifier == ParamTotalChargePower)
        	totalChargePower = GetFloat(rxMsg.data);
        else if(rxMsg.identifier == ParamTotalChargePowerSession)
        	totalChargePowerSession = GetFloat(rxMsg.data);
	    else if(rxMsg.identifier == ParamChargeMode)
	    	chargeMode = rxMsg.data[0];
	    else if(rxMsg.identifier == ParamChargeOperationMode)
        {
	    	chargeOperationMode = rxMsg.data[0];
	    	ESP_LOGW(TAG, "Dataset: T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  %.1fW %.3fWh CM: %d  COM: %d Timeouts: %i", temperatureEmeter[0], temperatureEmeter[1], temperatureEmeter[2], temperaturePowerBoardT[0], temperaturePowerBoardT[1], voltages[0], voltages[1], voltages[2], currents[0], currents[1], currents[2], totalChargePower, totalChargePowerSession, chargeMode, chargeOperationMode, mcuCommunicationError);
        }
	    else if(rxMsg.identifier == ParamHmiBrightness)
	    {
	    	ESP_LOGW(TAG, "**** Received HMI brightness ACK ****");
	    }
	    else if(rxMsg.identifier == DebugCounter)
	    	mcuDebugCounter = GetUint32_t(rxMsg.data);
		else if(rxMsg.identifier == ParamResetSource)
			mcuResetSource = rxMsg.data[0];
		else if(rxMsg.identifier == ParamWarnings)
			mcuWarnings = GetUint32_t(rxMsg.data);


        /*else if(rxMsg.type != 0)
        {
        	uint8_t error_code = ZDecodeUInt8(rxMsg.data);
        	printf("frame error code: %d\n\r", error_code);
        }*/
        freeZapMessageReply();

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
}


int MCU_GetSwitchState()
{
	ESP_LOGW(TAG, "**** Switch used: %d ****", receivedSwitchState);
	return receivedSwitchState;
}

//void MCU_SendParameter(uint16_t paramIdentifier, uint8_t * data, uint16_t length)
void MCU_SendParameter(uint16_t paramIdentifier, float data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;
	//txMsg.data = data;
	//txMsg.length = length;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];

	uint16_t encoded_length = ZEncodeMessageHeaderAndOneFloat(&txMsg, data, txBuf, encodedTxBuf);

//	uint encoded_length = ZEncodeMessageHeaderOnly(
//			   &txMsg, txBuf, encodedTxBuf
//		   );

   ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
   //runRequest(encodedTxBuf, encoded_length);
   freeZapMessageReply();

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


float MCU_GetPower()
{
	return totalChargePower;
}

float MCU_GetEnergy()
{
	return totalChargePowerSession;
}

uint8_t MCU_GetchargeMode()
{
	return chargeMode;
}

uint8_t MCU_GetChargeOperatingMode()
{
	return chargeOperationMode;
}


uint32_t MCU_GetDebugCounter()
{
	return mcuDebugCounter;
}
uint32_t MCU_GetWarnings()
{
	return mcuWarnings;
}
uint8_t MCU_GetResetSource()
{
	return mcuResetSource;
}

float MCU_GetMaxInstallationCurrentSwitch()
{
	return mcuMaxInstallationCurrentSwitch;
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
