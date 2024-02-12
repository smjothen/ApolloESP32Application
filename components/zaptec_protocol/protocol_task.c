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

#include "../i2c/include/i2cDevices.h"
#include "../../main/storage.h"
#include "../../main/sessionHandler.h"
#include "../../main/chargeController.h"
#include "../../main/chargeSession.h"

const char *TAG = "MCU            ";

static char mcuSwVersionString[20] = {0};
static char mcuGridTestString[32] = {0};
static float temperaturePowerBoardT[2]  = {0.0};
static float temperatureEmeter[3] = {0.0};
static float voltages[3] = {0.0};
static float currents[3] = {0.0};

static float totalChargePower = 0.0;
static float totalChargePowerSession = -1.0;

static int8_t chargeMode = eCAR_UNINITIALIZED;
static uint8_t chargeOperationMode = 0;

static uint32_t mcuDebugCounter = 0;
static uint32_t previousMcuDebugCounter = 0;

static uint32_t mcuWarnings = 0;
static bool mcuResetDetected = false;
static uint8_t mcuResetSource = 0;
static uint8_t mcuMode = 0;

static uint8_t mcuNetworkType = 0;
static uint8_t mcuCableType = 0;
static float mcuChargeCurrentUserMax = 0;
static uint16_t mcuPilotAvg = 0;
static uint16_t mcuProximityInst = 0;
static float mcuChargeCurrentInstallationMaxLimit = -1.0;
static float mcuStandAloneCurrent = -1.0;
static float mcuInstantPilotCurrent = 0;

static uint16_t espNotifications = 0;
static uint16_t mcuNotifications = 0;
static uint8_t receivedSwitchState = 0xFF;

int holdSetPhases = 0;
static uint8_t finalStopActive = 0;

typedef enum {
	PERIODIC_FLOAT,
	PERIODIC_U32,
	PERIODIC_BYTE,
	PERIODIC_CB,
} periodic_tx_type_t;

typedef void (*periodic_cb_t)(ZapMessage *m);

typedef struct {
	uint16_t id;
	periodic_tx_type_t type;
	void *var;
} periodic_tx_t;

void HandleReset(ZapMessage *msg) {
	if (msg->data[0]) {
		// TODO: Reset this bool after handling resending state to MCU
		mcuResetDetected = true;
		mcuResetSource = msg->data[0];

		// Simulate debug counter on ESP for Go Plus for now, based on whether or not
		// a new reset value gets delivered.
		previousMcuDebugCounter = mcuDebugCounter;
		mcuDebugCounter = 0;
	} else {
		mcuDebugCounter++;
	}
}

void HandleDebug(ZapMessage *msg) {
	previousMcuDebugCounter = mcuDebugCounter;
	mcuDebugCounter = GetUint32_t(msg->data);
}

static const periodic_tx_t periodic_tx[] = {
#ifdef GOPLUS
	// TODO: Add ParamMode to Go?
	{ ParamMode,                         PERIODIC_BYTE,  &mcuMode },
	{ MCUResetSource,                    PERIODIC_CB,    &HandleReset },
#else
	{ SwitchPosition,                    PERIODIC_BYTE,  &receivedSwitchState },
	{ ParamInternalTemperatureT2,        PERIODIC_FLOAT, &temperaturePowerBoardT[1] },
	{ ParamTotalChargePowerSession,      PERIODIC_FLOAT, &totalChargePowerSession },
	{ ChargeCurrentInstallationMaxLimit, PERIODIC_FLOAT, &mcuChargeCurrentInstallationMaxLimit },
	{ StandAloneCurrent,                 PERIODIC_FLOAT, &mcuStandAloneCurrent },
	{ DebugCounter,                      PERIODIC_CB,    &HandleDebug },
#endif
	{ ParamInternalTemperatureEmeter,    PERIODIC_FLOAT, &temperatureEmeter[0] },
	{ ParamInternalTemperatureEmeter2,   PERIODIC_FLOAT, &temperatureEmeter[1] },
	{ ParamInternalTemperatureEmeter3,   PERIODIC_FLOAT, &temperatureEmeter[2] },
	{ ParamInternalTemperatureT,         PERIODIC_FLOAT, &temperaturePowerBoardT[0] },
	{ ParamVoltagePhase1,                PERIODIC_FLOAT, &voltages[0] },
	{ ParamVoltagePhase2,                PERIODIC_FLOAT, &voltages[1] },
	{ ParamVoltagePhase3,                PERIODIC_FLOAT, &voltages[2] },
	{ ParamCurrentPhase1,                PERIODIC_FLOAT, &currents[0] },
	{ ParamCurrentPhase2,                PERIODIC_FLOAT, &currents[1] },
	{ ParamCurrentPhase3,                PERIODIC_FLOAT, &currents[2] },
	{ ParamTotalChargePower,             PERIODIC_FLOAT, &totalChargePower },
	{ ParamChargeMode,                   PERIODIC_BYTE,  &chargeMode },
	{ ParamChargeOperationMode,          PERIODIC_BYTE,  &chargeOperationMode },
	{ ParamWarnings,                     PERIODIC_U32,   &mcuWarnings },
	{ ParamNetworkType,                  PERIODIC_BYTE,  &mcuNetworkType },
	{ ParamCableType,                    PERIODIC_BYTE,  &mcuCableType },
	{ ParamChargeCurrentUserMax,         PERIODIC_FLOAT, &mcuChargeCurrentUserMax },
};

#define PERIODIC_TX_COUNT (sizeof (periodic_tx) / sizeof (periodic_tx[0]))

#define RX_TIMEOUT        (2000 / (portTICK_PERIOD_MS))
#define SEMAPHORE_TIMEOUT (20000 / (portTICK_PERIOD_MS))

static uint8_t MCU_ReadHwIdMCUSpeed();
static uint8_t MCU_ReadHwIdMCUPower();

void uartSendTask(void *pvParameters);
void uartRecvTask(void *pvParameters);
void configureUart();
void onCharRx(char c);

SemaphoreHandle_t uart_write_lock;
QueueHandle_t uart_recv_message_queue;
QueueHandle_t uart0_events_queue;
uint32_t mcuCommunicationError = 0;

static TaskHandle_t uartRecvTaskHandle = NULL;
static TaskHandle_t sendTaskHandle = NULL;

const int uart_num = UART_NUM_2;

void zaptecProtocolStart(){
    ESP_LOGI(TAG, "starting protocol task");
    static uint8_t ucParameterToPass = {0};
    int stack_size = 3000;//6000;//8192;//4096;
    xTaskCreate( uartRecvTask, "uartRecvTask", stack_size, &ucParameterToPass, 6, &uartRecvTaskHandle );
    configASSERT(uartRecvTaskHandle);
}

void dspic_periodic_poll_start(){
    static uint8_t ucParameterToPass = {0};
    int stack_size = 6000;//8192;
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


    esp_err_t err = uart_wait_tx_done(uart_num, RX_TIMEOUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART timeout! %d / %d : %d %d %d", sent_bytes, length, encodedTxBuf[0], ZDecodeUint16(&encodedTxBuf[1]), ZDecodeUint16(&encodedTxBuf[3]));
        for (int i = 0; i < length; i++) {
            printf("0x%02X ", encodedTxBuf[i]);
        }
        printf("\n");

    }

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

uint32_t GetUint32_t(uint8_t * input)
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

uint16_t GetUInt16(uint8_t * input)
{
	uint16_t tmp = 0;

	uint8_t swap[2] = {0};
	swap[0] = input[1];
	swap[1] = input[0];
	memcpy(&tmp, &swap[0], sizeof (swap));
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

	if (bVersion == 0x86) {
		// Go Plus
		return;
	}

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

void MCU_SendMaxCurrent()
{
	//Write MaxCurrent setting to MCU if restarted
	float currentInMaximum = storage_Get_CurrentInMaximum();
	if((32.0 >= currentInMaximum) && (currentInMaximum >= 0.0))
	{
		MessageType ret = 0;
		for (int i = 0; i < 3; i++)
		{
			ret = MCU_SendFloatParameter(ParamCurrentInMaximum, currentInMaximum);
			if(ret == MsgWriteAck)
			{
				ESP_LOGW(TAG, "Sent MaxCurrent to MCU after MCU start: %f \n", currentInMaximum);
				break;
			}
			else
			{
				ESP_LOGE(TAG, "Failed sending MaxCurrent to MCU after start");
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

static uint32_t offsetCount = 0;
void MCU_PrintReadings()
{
	ESP_LOGI(TAG, "T_EM: %3.2f %3.2f %3.2f  T_M: %3.2f %3.2f   V: %3.2f %3.2f %3.2f   I: %2.2f %2.2f %2.2f  %.1fW %.3fkWh CM: %d  COM: %d P:%d %.1f F: %d Timeouts: %" PRIi32 ", Off: %" PRId32 ", - %s, PP: %d, UC:%.1fA, MaxA:%2.1f, StaA: %2.1f, mN: 0x%X", temperatureEmeter[0], temperatureEmeter[1], temperatureEmeter[2], temperaturePowerBoardT[0], temperaturePowerBoardT[1], voltages[0], voltages[1], voltages[2], currents[0], currents[1], currents[2], totalChargePower, totalChargePowerSession, chargeMode, chargeOperationMode, IsChargingAllowed(), MCU_GetInstantPilotState(), finalStopActive, mcuCommunicationError, offsetCount, MCU_GetGridTypeString(), mcuCableType, mcuChargeCurrentUserMax, mcuChargeCurrentInstallationMaxLimit, mcuStandAloneCurrent, mcuNotifications);
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

    uint32_t count = 0;
    uint32_t printCount = 0;

	// ZEncodeMessageHeader* does not check the length of the buffer!
	// This should not be a problem for most usages, but make sure strings are within a range that fits!
	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];

    while (true) {
		const periodic_tx_t *tx = &periodic_tx[count % PERIODIC_TX_COUNT];
		printCount++;

		ZapMessage txMsg = {0};
		txMsg.type = MsgRead;
		txMsg.identifier = tx->id;

		uint16_t encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);

		ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
		freeZapMessageReply();

		if (txMsg.identifier != rxMsg.identifier) {
			ESP_LOGE(TAG, "**** DIFF: %d != %d ****", txMsg.identifier, rxMsg.identifier);
			offsetCount++;
		} else {

			if (tx->var) {
				switch(tx->type) {
					case PERIODIC_U32:
						*(uint32_t *)tx->var = GetUint32_t(rxMsg.data);
						break;
					case PERIODIC_BYTE:
						*(uint8_t *)tx->var = rxMsg.data[0];
						break;
					case PERIODIC_FLOAT:
						*(float *)tx->var = GetFloat(rxMsg.data);
						break;
					case PERIODIC_CB:
						((periodic_cb_t)(tx->var))(&rxMsg);
						break;
				}
			} else {
				ESP_LOGE(TAG, "**** UNHANDLED: %d ****", txMsg.identifier);
			}
		}

		if (txMsg.identifier == rxMsg.identifier) {
			count++;
			mcuComErrorCount = 0;
		} else {
			mcuComErrorCount++;
			ESP_LOGE(TAG, "mcuComErrorCount: %" PRIu32 "",mcuComErrorCount);
			//Delay before retrying on the same parameter identifier
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}

		if(printCount >= 25 * 5) {
			MCU_PrintReadings();
			printCount = 0;
		}

		if(count >= PERIODIC_TX_COUNT) {
			isMCUReady = true;
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

#define DEBUG_ZAP_PROTOCOL

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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

	return rxMsg.type;
}

MessageType MCU_SendCommandWithData(uint16_t paramIdentifier, const char *data, size_t length, uint8_t *errorCode)
{
	ZapMessage txMsg;
	txMsg.type = MsgCommand;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndByteArray(&txMsg, data, length, txBuf, encodedTxBuf);

	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	*errorCode = rxMsg.data[0];

	freeZapMessageReply();

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

	return rxMsg.type;
}

ZapMessage MCU_SendUint8WithReply(uint16_t paramIdentifier, uint8_t data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneByte(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

	return rxMsg;
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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

	return rxMsg.type;
}

ZapMessage MCU_SendUint32WithReply(uint16_t paramIdentifier, uint32_t data)
{
	ZapMessage txMsg;
	txMsg.type = MsgWrite;
	txMsg.identifier = paramIdentifier;

	uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
	uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
	uint16_t encoded_length = ZEncodeMessageHeaderAndOneUInt32(&txMsg, data, txBuf, encodedTxBuf);
	ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
	freeZapMessageReply();

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

	return rxMsg;
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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

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

#ifdef DEBUG_ZAP_PROTOCOL
	if (rxMsg.identifier != txMsg.identifier) { ESP_LOGI(TAG, "Rx.Id != Tx.Id : MsgType %d / MsgId %d", txMsg.type, txMsg.identifier); }
#endif

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
	MessageType ret = MCU_SendUint8Parameter(ParamLedOverrideClear, LED_CLEAR_WHITE);//Color defined here is no longer used actively in MCU
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


static hw_speed_revision HwIdSpeed = 0;
static uint8_t MCU_ReadHwIdMCUSpeed()
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

hw_speed_revision MCU_GetHwIdMCUSpeed()
{
	return HwIdSpeed;
}

static hw_power_revision HwIdPower = 0;
static uint8_t MCU_ReadHwIdMCUPower()
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

hw_power_revision MCU_GetHwIdMCUPower()
{
	return HwIdPower;
}

bool IsUKOPENPowerBoardRevision()
{
	if((HwIdPower == HW_POWER_3_UK) || (HwIdPower == HW_POWER_5_UK_X804))
		return true;
	else
		return false;
}

bool HasTamperDetection()
{
	if((HwIdSpeed == HW_SPEED_3_UK) || (HwIdSpeed == HW_SPEED_6_UK))
		return true;
	else
		return false;
}

bool IsProgrammableFPGAUsed()
{
	if((HwIdSpeed == HW_SPEED_3_UK) || (HwIdSpeed == HW_SPEED_5_EU) || (HwIdSpeed == HW_SPEED_6_UK) || (HwIdSpeed == HW_SPEED_7_EU))
		return true;
	else
		return false;
}


float MCU_GetOPENVoltage()
{
	ZapMessage rxMsgm = MCU_ReadParameter(ParamOPENVoltage);
	if((rxMsgm.length == 4) && (rxMsgm.identifier == ParamOPENVoltage))
	{
		float OPENVoltage = GetFloat(rxMsgm.data);
		//ESP_LOGI(TAG, "Read L1/O-PEN voltage: %f ", OPENVoltage);
		return OPENVoltage;
	}
	else
	{
		ESP_LOGE(TAG, "Read L1/O-PEN voltage FAILED");
		return -1.0;
	}
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

static bool sim_charge_op_enabled = false;
static uint8_t sim_charge_op_mode = CHARGE_OPERATION_STATE_UNINITIALIZED;

// Disable by setting mode to -1
void mcu_simulate_charge_op_mode(int mode) {
	if (mode < 0) {
		sim_charge_op_enabled = false;
	} else {
		sim_charge_op_enabled = true;
		sim_charge_op_mode = mode;
	}
}

uint8_t MCU_GetChargeOperatingMode()
{
	if (sim_charge_op_enabled) {
		return sim_charge_op_mode;
	}

	/// Used for Session reset
	if(overrideOpModeState == CHARGE_OPERATION_STATE_PAUSED)
		return CHARGE_OPERATION_STATE_PAUSED;

	/// Used for Session reset
	if(overrideOpModeState == CHARGE_OPERATION_STATE_DISCONNECTED)
		return CHARGE_OPERATION_STATE_DISCONNECTED;

	///Used to avoid sending requesting while in paused schedule state
	if((chargeOperationMode == CHARGE_OPERATION_STATE_REQUESTING) && (chargecontroller_IsPauseBySchedule() == true))
	{
		//ESP_LOGW(TAG, "# Replaced REQUESTING with PAUSED #");
		if(chargeSession_HasNewSessionId() == true)
			return CHARGE_OPERATION_STATE_PAUSED;
		else
			return CHARGE_OPERATION_STATE_REQUESTING;
	}

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

uint8_t MCU_ReadResetSource()
{
	ZapMessage rxMsgm = MCU_ReadParameter(MCUResetSource);
	if((rxMsgm.length == 1) && (rxMsgm.identifier == MCUResetSource))
	{
		mcuResetSource = rxMsgm.data[0];
		ESP_LOGW(TAG, "Read MCUResetSource: %d ", mcuResetSource);
		return 0;
	}
	else
	{
		ESP_LOGE(TAG, "Read MCUResetSource FAILED");
		return 1;
	}
}


void MCU_UpdateUseZaptecFinishedTimeout()
{
	uint8_t zft = 1;
	if(storage_Get_session_controller() == eSESSION_OCPP)
		zft = 0;

	ESP_LOGI(TAG, "Send ZFT %i command to MCU", zft);
	MessageType ret = MCU_SendUint8Parameter(ParamUseZaptecFinishedTimeout, zft);
	if(ret == MsgWriteAck)
	{
		ESP_LOGI(TAG, "OK");
	}
	else
	{
		ESP_LOGE(TAG, "FAILED");
	}
}




/*
 * Returns 100 if pilot state is 100% PWM
 * Returns 0 if plot state is 0% PWM
 * Otherwise returns current value communicated to car in A.
 */
float MCU_GetInstantPilotState()
{
	return mcuInstantPilotCurrent;
}

/*
 * For OCPP: If a car is connected and authorized then:
 * -false indicate SuspendedEVSE state - Charger does not allow charging
 * -true indicates SuspendedEV state - Charger allows charging and is waiting on the car
 */
bool IsChargingAllowed()
{
	float mcuInstantPilotCurrentHold = mcuInstantPilotCurrent;

	if(mcuInstantPilotCurrentHold == 100.0)
	{
		return false;
	}
	else
	{
		// Also State F - 0.0 is a temporary car wakeup state and should be regarded as charging allowed
		return true;
	}
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

char * MCU_GetGridTypeString() {
	char *netString = "Non ";
	if (mcuNetworkType == 1) {
		netString = "IT_1";
	} else if (mcuNetworkType == 2) {
		netString = "IT_3";
	} else if (mcuNetworkType == 3) {
		netString = "TN_1";
	} else if (mcuNetworkType == 4) {
		netString = "TN_3";
	}
	return netString;
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


bool MCU_GetEmeterSnapshot(int param, uint8_t *source, float *ret) {
	ZapMessage rxMsgm = MCU_ReadParameter(param);
	if((rxMsgm.length == (1+3*4)) && (rxMsgm.identifier == param)) {
      uint8_t *data = rxMsgm.data;
      *source = *data++;
      ret[0] = GetFloat(data);
      data += 4;
      ret[1] = GetFloat(data);
      data += 4;
      ret[2] = GetFloat(data);
      return true;
  }

  return false;
}

int16_t MCU_GetServoCheckParameter(int parameterDefinition)
{
	int16_t servoCheckParameter = 0;
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


bool MCU_GetAutoClearStatus(uint32_t *timeout, uint16_t *count, uint16_t *totalCount) {
    ZapMessage msg = MCU_ReadParameter(ParamAutoClearState);
    if (msg.length != 8 || msg.identifier != ParamAutoClearState) {
        return false;
    }

    *timeout = GetUint32_t(msg.data);
    *count = GetUInt16(msg.data + 4);
    *totalCount = GetUInt16(msg.data + 6);
    return true;
}

void MCU_GetOPENSamples(char * samples)
{
	if(MsgCommandAck == MCU_SendCommandId(CommandGetOPENSamples))
	{
		vTaskDelay(pdMS_TO_TICKS(1500));
		ZapMessage rxMsg = MCU_ReadParameter(ParamDiagnosticsString);
		ESP_LOGW(TAG, "rxMsg.length: %i",  rxMsg.length);
		if(rxMsg.length == 114)
		{
			int i;
			for (i = 0; i < rxMsg.length; i=i+3)
			{
				snprintf(samples + strlen(samples), 160, "%c%c%c ", rxMsg.data[i], rxMsg.data[i+1], rxMsg.data[i+2]);
			}
			ESP_LOGW(TAG, "Samples: %s",  samples);

		}
		else
		{
			snprintf(samples, 50, "Nr of Samples: %i", rxMsg.length);
		}
	}
}

uint8_t MCU_GetRelayStates()
{
	ZapMessage rxMsg = MCU_ReadParameter(RelayStates);
	uint8_t relayStates = 0xFF;
	if((rxMsg.length == 1) && (rxMsg.identifier == RelayStates))
		relayStates = rxMsg.data[0];
	return relayStates;
}

uint8_t MCU_GetRCDButtonTestStates()
{
	ZapMessage rxMsg = MCU_ReadParameter(RCDButtonTestState);
	uint8_t buttonState = 0;
	if((rxMsg.length == 1) && (rxMsg.identifier == RCDButtonTestState))
		buttonState = rxMsg.data[0];
	return buttonState;
}

void MCU_GetFPGAInfo(char *stringBuf, int maxTotalLen)
{
	ZapMessage rxMsg = MCU_ReadParameter(ParamFpgaVersionAndHash);
	if((rxMsg.length > 0) && (rxMsg.length < maxTotalLen))
	{
		strncpy(stringBuf, (char*)rxMsg.data, rxMsg.length);
		ESP_LOGI(TAG, "%s", stringBuf);
	}
}

bool MCU_ClearWarning(uint32_t warning) {
	return MCU_SendUint32Parameter(ParamClearWarning, warning) == MsgWriteAck;
}

/*
 * Required in OCPP mode according to OCPP 1.6j specification to unlock
 * handle remotely in state B.
 */
bool MCU_SendCommandServoForceUnlock()
{
	if(MsgCommandAck == MCU_SendCommandId(CommandServoForceUnlock))
	{
		ESP_LOGI(TAG, "Sent CommandServoForceUnlock OK");
		return true;
	}
	else
	{
		ESP_LOGE(TAG, "Sent CommandServoForceUnlock FAILED");
		return false;
	}
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

/*
 * Mask bit 0 from MCU to avoid Cloud message every time it toggles. The toggling is counted in the HandleNotifictions()-function
 */
uint32_t GetCombinedNotificationsMasked()
{
	return (uint32_t)((espNotifications << 16) + (mcuNotifications & ~0x1));
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
