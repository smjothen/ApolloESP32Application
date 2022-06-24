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
#include "../apollo_ota/include/pic_update.h"
#include "../../main/DeviceInfo.h"
#include "../i2c/include/i2cDevices.h"
#include "../../main/storage.h"
#include "../../main/sessionHandler.h"
#include "../../main/chargeController.h"

const char *TAG = "MCU            ";

#define RX_TIMEOUT  (2000 / (portTICK_PERIOD_MS))
#define SEMAPHORE_TIMEOUT  (20000 / (portTICK_PERIOD_MS))


void uartSendTask(void *pvParameters);
void uartRecvTask(void *pvParameters);
void configureUart();
void onCharRx(char c);

SemaphoreHandle_t uart_write_lock;
QueueHandle_t uart_recv_message_queue;
QueueHandle_t uart0_events_queue;
uint32_t mcuCommunicationError = 0;
uint8_t receivedSwitchState = 0xFF;

static TaskHandle_t uartRecvTaskHandle = NULL;
static TaskHandle_t sendTaskHandle = NULL;

const int uart_num = UART_NUM_2;

void zaptecProtocolStart(){
    ESP_LOGI(TAG, "starting protocol task");
    static uint8_t ucParameterToPass = {0};
    int stack_size = 6000;//8192;//4096;
    xTaskCreate( uartRecvTask, "uartRecvTask", stack_size, &ucParameterToPass, 6, &uartRecvTaskHandle );
    configASSERT(uartRecvTaskHandle);
}

void dspic_periodic_poll_start(){
    static uint8_t ucParameterToPass = {0};
    int stack_size = 8192;
    xTaskCreate( uartSendTask, "UARTSendTask", stack_size, &ucParameterToPass, 5, &sendTaskHandle );
    configASSERT( sendTaskHandle );
}


void protocol_task_ctrl_debug(int state)
{
	if(state == 0)
		esp_log_level_set(TAG, ESP_LOG_NONE);
	else
		esp_log_level_set(TAG, ESP_LOG_INFO);
}


//For testing: insert junk data to test robustness
//int junkTrig = 0;
//uint8_t junkCount = 0;
//char junkVal[2] = {0};

ZapMessage runRequest(const uint8_t *encodedTxBuf, uint length){

    if( xSemaphoreTake( uart_write_lock, SEMAPHORE_TIMEOUT ) == pdTRUE )
    {
    	uart_flush_input(uart_num);
        xQueueReset(uart_recv_message_queue);

        int sent_bytes = uart_write_bytes(uart_num, (char *)encodedTxBuf, length);
		if(sent_bytes<length){
			ESP_LOGE(TAG, "Failed to send all bytes (%d/%d)", sent_bytes, length);
		}
		ESP_ERROR_CHECK(uart_wait_tx_done(uart_num, RX_TIMEOUT)); // tx flush. portMAX_DELAY causes the system to hang indefinitely, use a 2s timeout as  workaround. https://github.com/espressif/esp-idf/issues/5156

        ZapMessage rxMsg = {0};
        if( xQueueReceive( 
				uart_recv_message_queue,
				&( rxMsg ),
				RX_TIMEOUT) == pdFALSE){
					ESP_LOGE(TAG, "timeout in response to runRequest()");
		}
        

        // dont release uart_write_lock, let caller use freeZapMessageReply()
        return rxMsg;
    }
    //configASSERT(false);
	ESP_LOGE(TAG, "failed to obtain uart_write_lock");
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
                    if(xQueueSend(
                        uart_recv_message_queue,

						//rxMsg is copied, so that consumers of the queue may edit the data, 
						// they can also be certain it will not be changed by other tasks                        
                        ( void * ) &rxMsg,
						// do not block the task if the queue is not ready. It will cause
						// the queue to be unable to xQueueReset properly, since the task itself will
						// also hold a message
                        0 
                    )){
						// message sent immediately
					}else{
						ESP_LOGW(TAG, "there is already a ZapMessage in the queue, this indicates a syncronization issue");
					}
                }
            }
        }
}

static char mcuSwVersionString[20] = {0};
static char mcuGridTestString[32] = {0};
static float temperaturePowerBoardT[2]  = {0.0};
static float temperatureEmeter[3] = {0.0};
static float voltages[3] = {0.0};
static float currents[3] = {0.0};

static float totalChargePower = 0.0;
static float totalChargePowerSession = -1.0;
static float max_reported_energy = -1.0;

static int8_t chargeMode = eCAR_UNINITIALIZED;
static uint8_t chargeOperationMode = 0;

static uint32_t mcuDebugCounter = 0;
static uint32_t previousMcuDebugCounter = 0;

static uint32_t mcuWarnings = 0;
static uint8_t mcuResetSource = 0;

static uint8_t mcuNetworkType = 0;
static char mcuNetworkTypeString[5] = {0};
static uint8_t mcuCableType = 0;
static float mcuChargeCurrentUserMax = 0;
static uint16_t mcuPilotAvg = 0;
static uint16_t mcuProximityInst = 0;
static float mcuChargeCurrentInstallationMaxLimit = -1.0;
static float mcuStandAloneCurrent = -1.0;

static uint16_t espNotifications = 0;
static uint16_t mcuNotifications = 0;

int holdSetPhases = 0;
static uint8_t finalStopActive = 0;

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

int MCURxGetStackWatermark()
{
	if(uartRecvTaskHandle != NULL)
		return uxTaskGetStackHighWaterMark(uartRecvTaskHandle);
	else
		return -1;
}

int MCUTxGetStackWatermark()
{
	if(sendTaskHandle != NULL)
		return uxTaskGetStackHighWaterMark(sendTaskHandle);
	else
		return -1;
}


void ActivateMCUWatchdog()
{
	//If the bootloader version does not have the very high watchdog frequency - activate the watchdog.
	uint8_t bVersion = get_bootloader_version();
	ESP_LOGW(TAG, "Bootloader version is: %d", bVersion);

	int chargerNumber = 0;
	if(strstr(i2cGetLoadedDeviceInfo().serialNumber, "ZAP"))
	{
		char *endptr;
		chargerNumber = (int)strtol(i2cGetLoadedDeviceInfo().serialNumber+3, &endptr, 10);
	}

	if(((0xff > bVersion) && (bVersion >= 6)) || (chargerNumber > 60) || (chargerNumber == 25))
	{
		bool watchdogSet = false;
		uint8_t timeout = 10;
		while((watchdogSet == false) && (timeout > 0))
		{

			MessageType ret = MCU_SendCommandId(CommandActivateWatchdog);
			if(ret == MsgCommandAck)
			{
				watchdogSet = true;
				ESP_LOGW(TAG, "MCU watchdog OK");
			}
			else
			{
				watchdogSet = false;
				ESP_LOGE(TAG, "MCU watchdog FAILED");

				timeout--;
				if(timeout == 0)
					SetEspNotification(eNOTIFICATION_MCU_WATCHDOG);

				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
		}
	}
}

bool isMCUReady = false;

//Call this to see if all MCU parametes has been received at start, before communicating to cloud
bool MCU_IsReady()
{
	return isMCUReady;
}

uint32_t mcuComErrorCount = 0;

void uartSendTask(void *pvParameters){


    //Provide application time to initialize before sending to MCU
    uint8_t timeout = 10;
    while((i2CDeviceInfoIsLoaded() == false) && (timeout > 0))
    {
    	timeout--;
    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    	ESP_LOGI(TAG, "Configuring MCU uart waiting for initialization: %d", timeout);
    }

    //Read mcu application software at startup. Try up to 5 times
    uint8_t timout = 5;
    while(timout > 0)
    {
    	ZapMessage rxMsg;
    	rxMsg = MCU_ReadParameter(ParamSmartMainboardAppSwVersion);
    	if((20>=rxMsg.length) && (rxMsg.length > 1))
    	{
    		strncpy(mcuSwVersionString, (char*)rxMsg.data, rxMsg.length);
    		ESP_LOGW(TAG, "MCU sw version: %s", mcuSwVersionString);
    		break;
    	}
    	timout--;

    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }


    //Only applies to chargers with MCU bootloaders version 6 and greater.
    ActivateMCUWatchdog();

    MCU_UpdateOverrideGridType();
    MCU_UpdateIT3OptimizationState();
    MCU_ReadHwIdMCUSpeed();
    MCU_ReadHwIdMCUPower();

    uint32_t count = 0;
    uint32_t printCount = 0;
    uint32_t offsetCount = 0;
    while (true)
    {
    	if(mcuDebugCounter < previousMcuDebugCounter)
    	{
    		ActivateMCUWatchdog();
    		ESP_LOGW(TAG, "MCU restart detected");
    		previousMcuDebugCounter = mcuDebugCounter;
    	}

        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        
        printCount++;

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
				txMsg.identifier = MCUResetSource;
				break;
			case 18:
				txMsg.identifier = ParamWarnings;
				break;

			case 19:
				txMsg.identifier = ParamNetworkType;
				break;
			case 20:
				txMsg.identifier = ParamCableType;
				break;
			case 21:
				txMsg.identifier = ParamChargeCurrentUserMax;
				break;

			case 22:
				txMsg.identifier = ChargeCurrentInstallationMaxLimit;
				break;
			case 23:
				txMsg.identifier = StandAloneCurrent;
				break;


        	/*default:
        		vTaskDelay(1000 / portTICK_PERIOD_MS);
        		continue;
        		break;*/
        }




        txMsg.type = MsgRead;

        uint16_t encoded_length = ZEncodeMessageHeaderOnly(
                    &txMsg, txBuf, encodedTxBuf
                );

        //ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
        
        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
        freeZapMessageReply();
        //printf("frame type: %d \n\r", rxMsg.type);
        //printf("frame identifier: %d \n\r", rxMsg.identifier);
//        printf("frame timeId: %d \n\r", rxMsg.timeId);


        if(txMsg.identifier != rxMsg.identifier)
        {
        	ESP_LOGE(TAG, "**** DIFF: %d != %d ******\n\r", txMsg.identifier, rxMsg.identifier);
        	offsetCount++;
        }

        if(rxMsg.identifier == SwitchPosition)
        	receivedSwitchState = rxMsg.data[0];
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
			if(max_reported_energy<totalChargePowerSession)
				max_reported_energy = totalChargePowerSession;

	    else if(rxMsg.identifier == ParamChargeMode)
	    	chargeMode = rxMsg.data[0];
	    else if(rxMsg.identifier == ParamChargeOperationMode)
        {
	    	chargeOperationMode = rxMsg.data[0];
	    	//ESP_LOGW(TAG, "T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  %.1fW %.3fWh CM: %d  COM: %d Timeouts: %i, Off: %d", temperatureEmeter[0], temperatureEmeter[1], temperatureEmeter[2], temperaturePowerBoardT[0], temperaturePowerBoardT[1], voltages[0], voltages[1], voltages[2], currents[0], currents[1], currents[2], totalChargePower, totalChargePowerSession, chargeMode, chargeOperationMode, mcuCommunicationError, offsetCount);
        }
	    else if(rxMsg.identifier == HmiBrightness)
	    {
	    	ESP_LOGW(TAG, "**** Received HMI brightness ACK ****");
	    }
	    else if(rxMsg.identifier == DebugCounter)
	    {
	    	previousMcuDebugCounter = mcuDebugCounter;
	    	mcuDebugCounter = GetUint32_t(rxMsg.data);
	    }
		else if(rxMsg.identifier == MCUResetSource)
			mcuResetSource = rxMsg.data[0];
		else if(rxMsg.identifier == ParamWarnings)
			mcuWarnings = GetUint32_t(rxMsg.data);

		else if(rxMsg.identifier == ParamNetworkType)
		{
			mcuNetworkType = rxMsg.data[0];
			if(mcuNetworkType == 0)
				memcpy(mcuNetworkTypeString, "Non ",4);
			else if(mcuNetworkType == 1)
				memcpy(mcuNetworkTypeString, "IT_1",4);
			else if(mcuNetworkType == 2)
				memcpy(mcuNetworkTypeString, "IT_3",4);
			else if(mcuNetworkType == 3)
				memcpy(mcuNetworkTypeString, "TN_1",4);
			else if(mcuNetworkType == 4)
				memcpy(mcuNetworkTypeString, "TN_3",4);
		}
		else if(rxMsg.identifier == ParamCableType)
			mcuCableType = rxMsg.data[0];
		else if(rxMsg.identifier == ParamChargeCurrentUserMax)
			mcuChargeCurrentUserMax = GetFloat(rxMsg.data);

		else if(rxMsg.identifier == ChargeCurrentInstallationMaxLimit)
			mcuChargeCurrentInstallationMaxLimit =  GetFloat(rxMsg.data);
			//mcuPilotAvg = (rxMsg.data[0] << 8) | rxMsg.data[1];
		else if(rxMsg.identifier == StandAloneCurrent)
		{
			mcuStandAloneCurrent =  GetFloat(rxMsg.data);
			isMCUReady = true;
		}
			//mcuProximityInst = (rxMsg.data[0] << 8) | rxMsg.data[1];




        /*else if(rxMsg.type != 0)
        {
        	uint8_t error_code = ZDecodeUInt8(rxMsg.data);
        	printf("frame error code: %d\n\r", error_code);
        }*/

        if (txMsg.identifier == rxMsg.identifier)
        {
        	count++;
        	mcuComErrorCount = 0;
        }
        else
        {
        	mcuComErrorCount++;

       		ESP_LOGE(TAG, "mcuComErrorCount: %i",mcuComErrorCount);

        	//Delay before retrying on the same parameter identifier
        	vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        if(printCount >= 24 * 5)//15)
        {
        	ESP_LOGI(TAG, "T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  %.1fW %.3fkWh CM: %d  COM: %d Timeouts: %i, Off: %d, - %s, PP: %d, UC:%.1fA, MaxA:%2.1f, StaA: %2.1f", temperatureEmeter[0], temperatureEmeter[1], temperatureEmeter[2], temperaturePowerBoardT[0], temperaturePowerBoardT[1], voltages[0], voltages[1], voltages[2], currents[0], currents[1], currents[2], totalChargePower, totalChargePowerSession, chargeMode, chargeOperationMode, mcuCommunicationError, offsetCount, mcuNetworkTypeString, mcuCableType, mcuChargeCurrentUserMax, mcuChargeCurrentInstallationMaxLimit, mcuStandAloneCurrent);
        	printCount = 0;
        }

        if(count >= 24)
        {
        	vTaskDelay(1000 / portTICK_PERIOD_MS);
        	count = 0;
        	continue;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
}


uint32_t GetMCUComErrors()
{
	return mcuComErrorCount;
}


MessageType MCU_SendCommandId(uint16_t paramIdentifier)
{
	ZapMessage txMsg;
	txMsg.type = MsgCommand;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg.type;
}


MessageType MCU_SendUint8Parameter(uint16_t paramIdentifier, uint8_t data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneByte(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg.type;
}


MessageType MCU_SendUint16Parameter(uint16_t paramIdentifier, uint16_t data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneUInt16(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg.type;
}


MessageType MCU_SendUint32Parameter(uint16_t paramIdentifier, uint32_t data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneUInt32(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg.type;
}



MessageType MCU_SendFloatParameter(uint16_t paramIdentifier, float data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneFloat(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg.type;
}


ZapMessage MCU_ReadParameter(uint16_t paramIdentifier)
{
	ZapMessage txMsg;
	txMsg.type = MsgRead;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

	return rxMsg;
}


void MCU_StartLedOverride()
{
	ESP_LOGI(TAG, "Send white pulsing command to MCU");
	MessageType ret = MCU_SendUint8Parameter(ParamLedOverride, LED_CLEAR_WHITE_BLINKING);
	if(ret == MsgWriteAck)
	{
		ESP_LOGI(TAG, "MCU white pulsing OK. ");
	}
	else
	{
		ESP_LOGI(TAG, "MCU white pulsing FAILED");
	}
}

void MCU_StopLedOverride()
{
	ESP_LOGI(TAG, "Clear overriding LED on MCU");
	MessageType ret = MCU_SendUint8Parameter(ParamLedOverrideClear, LED_CLEAR_WHITE);
	if(ret == MsgWriteAck)
	{
		ESP_LOGI(TAG, "MCU Cleared ledoverride OK");
	}
	else
	{
		ESP_LOGI(TAG, "MCU clearing ledoverride FAILED");
	}
}


static uint8_t overrideGridType = 0;
uint8_t MCU_UpdateOverrideGridType()
{
	ZapMessage rxMsgm = MCU_ReadParameter(ParamGridTypeOverride);
	if((rxMsgm.length == 1) && (rxMsgm.identifier == ParamGridTypeOverride))
	{
		overrideGridType = rxMsgm.data[0];
		ESP_LOGW(TAG, "Read overrideGridType: %d ", overrideGridType);
		return overrideGridType;
	}
	else
	{
		ESP_LOGE(TAG, "Read overrideGridType FAILED");
		return 0xff;
	}
}

uint8_t MCU_GetOverrideGridType()
{
	return overrideGridType;
}



static uint8_t IT3OptimizationEnabled = 0;
uint8_t MCU_UpdateIT3OptimizationState()
{
	ZapMessage rxMsgm = MCU_ReadParameter(ParamIT3OptimizationEnabled);
	if((rxMsgm.length == 1) && (rxMsgm.identifier == ParamIT3OptimizationEnabled))
	{
		IT3OptimizationEnabled = rxMsgm.data[0];
		ESP_LOGW(TAG, "Read IT3OptimizationEnabled: %d ", IT3OptimizationEnabled);
		return 0;
	}
	else
	{
		ESP_LOGE(TAG, "Read IT3OptimizationEnabled FAILED");
		return 1;
	}
}

uint8_t MCU_GetIT3OptimizationState()
{
	return IT3OptimizationEnabled;
}


char * MCU_GetSwVersionString()
{
	return mcuSwVersionString;
}


static uint8_t HwIdSpeed = 0;
uint8_t MCU_ReadHwIdMCUSpeed()
{
	ZapMessage rxMsgm = MCU_ReadParameter(HwIdMCUSpeed);
	if((rxMsgm.length == 1) && (rxMsgm.identifier == HwIdMCUSpeed))
	{
		HwIdSpeed = rxMsgm.data[0];
		ESP_LOGW(TAG, "Read HwIdSpeed: %d ", HwIdSpeed);
		return 0;
	}
	else
	{
		ESP_LOGE(TAG, "Read HwIdMCUSpeed FAILED");
		return 1;
	}
}

uint8_t MCU_GetHwIdMCUSpeed()
{
	return HwIdSpeed;
}

static uint8_t HwIdPower = 0;
uint8_t MCU_ReadHwIdMCUPower()
{
	ZapMessage rxMsgm = MCU_ReadParameter(HwIdMCUPower);
	if((rxMsgm.length == 1) && (rxMsgm.identifier == HwIdMCUPower))
	{
		HwIdPower = rxMsgm.data[0];
		ESP_LOGW(TAG, "Read HwIdPower: %d ", HwIdPower);
		return 0;
	}
	else
	{
		ESP_LOGE(TAG, "Read HwIdPower FAILED");
		return 1;
	}
}

uint8_t MCU_GetHwIdMCUPower()
{
	return HwIdPower;
}







char * MCU_GetGridTestString()
{
	return mcuGridTestString;
}



uint8_t MCU_GetSwitchState()
{
	//ESP_LOGW(TAG, "**** Switch used: %d ****", receivedSwitchState);
	return receivedSwitchState;
}


float MCU_GetEmeterTemperature(uint8_t phase)
{
	return temperatureEmeter[phase];
}

float MCU_GetTemperaturePowerBoard(uint8_t sensor)
{
	return temperaturePowerBoardT[sensor];
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
	//Do not allow negative power, have seen small fluctuations when ~0A
	if(totalChargePower >= 0.0)	//Watts
		return totalChargePower;
	else
		return 0.0;
}

float MCU_GetEnergy()
{
	//Becomes 0 when car disconnects
	return totalChargePowerSession;
}

float MCU_GetMaximumEnergy(){
	return max_reported_energy;
}

void MCU_ClearMaximumEnergy(){
	max_reported_energy = totalChargePowerSession;
}

int8_t MCU_GetChargeMode()
{
	return chargeMode;
}


static enum ChargerOperatingMode overrideOpModeState = CHARGE_OPERATION_STATE_UNINITIALIZED;
void SetTransitionOperatingModeState(enum ChargerOperatingMode newTransitionState)
{
	overrideOpModeState = newTransitionState;
}
enum ChargerOperatingMode GetTransitionOperatingModeState()
{
	return overrideOpModeState;
}

uint8_t MCU_GetChargeOperatingMode()
{

	/// Used for Session reset
	if(overrideOpModeState == CHARGE_OPERATION_STATE_PAUSED)
		return CHARGE_OPERATION_STATE_PAUSED;

	/// Used for Session reset
	if(overrideOpModeState == CHARGE_OPERATION_STATE_DISCONNECTED)
		return CHARGE_OPERATION_STATE_DISCONNECTED;

	///Used to avoid sending requesting while in paused schedule state
	if((chargeOperationMode == CHARGE_OPERATION_STATE_REQUESTING) && (chargecontroller_IsPauseBySchedule() == true))
	{
		ESP_LOGE(TAG, "# Replaced REQUESTING with PAUSED #");
		return CHARGE_OPERATION_STATE_PAUSED;
	}

	/*else if((chargeOperationMode == CHARGE_OPERATION_STATE_PAUSED) && (chargecontroller_IsPauseBySchedule() == false) && chargeController_IsScheduleActive() && chargeController_DoResumeCharging())// && GetFinalStopActiveStatus())
	{
		ESP_LOGE(TAG, "# Replaced PAUSED with REQUESTING #");
		return CHARGE_OPERATION_STATE_REQUESTING;
	}*/

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
	float maxCurrent = 0.0;

    switch(receivedSwitchState)
    {
        case 0:
            maxCurrent = 0.0;
            break;
        case 1:
            maxCurrent = 6.0;
            break;
        case 2:
            maxCurrent = 10.0;
            break;
        case 3:
            maxCurrent = 13.0;
            break;
        case 4:
            maxCurrent = 16.0;
            break;
        case 5:
            maxCurrent = 20.0;
            break;
        case 6:
            maxCurrent = 25.0;
            break;
        case 7:
            maxCurrent = 32.0;
            break;
        case 8:
            maxCurrent = 0.0;
            break;
        case 9:
            maxCurrent = 0.0;
            break;
        default:
            maxCurrent = 0.0;
            break;
    }

	return maxCurrent;
}

char * MCU_GetGridTypeString()
{
	return mcuNetworkTypeString;
}

uint8_t MCU_GetGridType()
{
	return mcuNetworkType;
}


float MCU_GetChargeCurrentUserMax()
{
	return mcuChargeCurrentUserMax;
}

void HOLD_SetPhases(int setPhases)
{
	holdSetPhases = setPhases;
}
int HOLD_GetSetPhases()
{
	//In system mode the holdPhases is set from Cloud.
	//In standalone the charging ID must be based on network type and phase rotation
	if(storage_Get_Standalone())
	{
		//Return charging ID for 1P3W
		if(mcuNetworkType == NETWORK_1P3W)
		{
			if(storage_Get_PhaseRotation() == 10) 		//L1-L3
				return 8;
			else if(storage_Get_PhaseRotation() == 11)	//L2-L3
				return 6;
			else if(storage_Get_PhaseRotation() == 12)	//L1-L2
				return 5;
		}

		//Return charging ID for 3P3W
		else if(mcuNetworkType == NETWORK_3P3W)
		{
			return 9;
		}

		//Return charging ID for 1P4W
		else if(mcuNetworkType == NETWORK_1P4W)
		{
			if(storage_Get_PhaseRotation() == 1) 		//L1-N
				return 1;
			else if(storage_Get_PhaseRotation() == 2)	//L2-N
				return 2;
			else if(storage_Get_PhaseRotation() == 3)	//L3-N
				return 3;
		}
		//Return charging ID for 3P4W
		else if(mcuNetworkType == NETWORK_3P4W)
		{
			return 4;
		}

		else
		{
			return 0;
		}

	}

	return holdSetPhases;
}

//On Pro the maximum number of phases can be configured.
//On Go it depends on the measured wiring
uint8_t GetMaxPhases()
{
	if((mcuNetworkType == NETWORK_1P3W) || (mcuNetworkType == NETWORK_1P4W))
		return 1;
	else if((mcuNetworkType == NETWORK_3P3W) || (mcuNetworkType == NETWORK_3P4W))
		return 3;
	else
		return 0;
}

uint8_t MCU_GetCableType()
{
	return mcuCableType;
}


uint16_t MCU_GetPilotAvg()
{
	return mcuPilotAvg;
}

uint16_t MCU_ProximityInst()
{
	return mcuProximityInst;
}

//This is used to show if the switch or BLE was used to configure the max current
uint8_t maxCurrentConfiguredBy = 0;

float MCU_ChargeCurrentInstallationMaxLimit()
{
	float switchCurrent = MCU_GetMaxInstallationCurrentSwitch();

	if(mcuChargeCurrentInstallationMaxLimit > 0.0)
	{
		maxCurrentConfiguredBy = 2; //2 = BLE
		return mcuChargeCurrentInstallationMaxLimit;
	}
	else if(switchCurrent > 0.0)
	{
		maxCurrentConfiguredBy = 1;	//1 = Switch
		return switchCurrent;
	}
	else
	{
		maxCurrentConfiguredBy = 0; //0 = Unconfigured
		return 0.0;
	}
}


uint8_t GetMaxCurrentConfigurationSource()
{
	return maxCurrentConfiguredBy;
}

float MCU_StandAloneCurrent()
{
	return mcuStandAloneCurrent;
}





uint16_t MCU_GetServoCheckParameter(int parameterDefinition)
{
	uint16_t servoCheckParameter = 0;
	ZapMessage rxMsgm = MCU_ReadParameter(parameterDefinition);
	if((rxMsgm.length == 2) && (rxMsgm.identifier == parameterDefinition))
	{
		servoCheckParameter = rxMsgm.data[0] << 8 | rxMsgm.data[1];
		if(parameterDefinition == ServoCheckStartPosition)
			ESP_LOGW(TAG, "Read ServoCheckStartPosition: %d ", servoCheckParameter);
		else if(parameterDefinition == ServoCheckStartCurrent)
			ESP_LOGW(TAG, "Read ServoCheckStartPosition: %d ", servoCheckParameter);
		else if(parameterDefinition == ServoCheckStopPosition)
			ESP_LOGW(TAG, "Read ServoCheckStartPosition: %d ", servoCheckParameter);
		else if(parameterDefinition == ServoCheckStopCurrent)
			ESP_LOGW(TAG, "Read ServoCheckStartPosition: %d ", servoCheckParameter);
	}
	else
	{
		ESP_LOGE(TAG, "Read servoCheck param %i FAILED", parameterDefinition);
		return 0xff;
	}

	return servoCheckParameter;
}

static bool servoCheckRunning = false;
bool MCU_ServoCheckRunning()
{
	return servoCheckRunning;
}

void MCU_ServoCheckClear()
{
	servoCheckRunning = false;
}

void MCU_PerformServoCheck()
{
	if(MsgCommandAck == MCU_SendCommandId(CommandStartServoCheck))
	{
		ESP_LOGW(TAG, "Sent CommandStartServoCheck OK");
		servoCheckRunning = true;
	}
	else
	{
		ESP_LOGE(TAG, "Sent CommandStartServoCheck FAILED");
	}
}


float MCU_GetHWCurrentActiveLimit()
{
	float limit = -1.0;
	ZapMessage rxMsg = MCU_ReadParameter(HWCurrentActiveLimit);
	if((rxMsg.length == 4) && (rxMsg.identifier == HWCurrentActiveLimit))
	{
		limit = GetFloat(rxMsg.data);
	}

	return limit;
}

float MCU_GetHWCurrentMaxLimit()
{
	float limit = -1.0;
	ZapMessage rxMsg = MCU_ReadParameter(HWCurrentMaxLimit);
	if((rxMsg.length == 4) && (rxMsg.identifier == HWCurrentMaxLimit))
	{
		limit = GetFloat(rxMsg.data);
	}

	return limit;
}





void SetEspNotification(uint16_t notification)
{
	espNotifications |= notification;
}

void ClearNotifications()
{
	espNotifications = 0;
}

uint32_t GetCombinedNotifications()
{
	return (uint32_t)((espNotifications << 16) + mcuNotifications);
}

void SetFinalStopActiveStatus(uint8_t status)
{
	finalStopActive = status;
}

uint8_t GetFinalStopActiveStatus()
{
	return finalStopActive;
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
