#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "chargeController.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "freertos/timers.h"


static const char *TAG = "CHARGECONTROL  ";


const uint32_t maxStartDelay = 600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
static bool runStartimer = false;
static bool overrideTimer = false;

void chargeController_Init()
{
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(1000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);
}

void chargeController_CancelDelay()
{
	if(runStartimer == true)
	{
		overrideTimer = true;
	}
}

static void RunStartChargeTimer()
{
	if((startDelayCounter > 0) && (runStartimer == true))
	{
		startDelayCounter--;

		ESP_LOGW(TAG, "startDelayCounter %i/%i, Override: %i", startDelayCounter, randomStartDelay, overrideTimer);

		if((startDelayCounter == 0) || (overrideTimer == true))
		{
			chargeController_SendStartCommandToMCU();

			runStartimer = false;
			overrideTimer = false;
			startDelayCounter = 0;
		}
	}
}


void chargeController_SetStartTimer()
{
	/// Formula: int randomStartDelay = (esp_random() % (high - low + 1)) + low;
	randomStartDelay = (esp_random() % (maxStartDelay + 1));
	startDelayCounter = randomStartDelay;

	ESP_LOGW(TAG, "startDelayCounter set to %i", startDelayCounter);

	runStartimer = true;
}


static bool chargeController_SendStartCommandToMCU()
{
	MessageType ret = MCU_SendCommandId(CommandStartCharging);
	if(ret == MsgCommandAck)
	{
		ESP_LOGW(TAG, "Sent start Command to MCU OK");
		return true;
	}
	else
	{
		ESP_LOGW(TAG, "Sent start Command to MCU FAILED");
		return false;
	}
}


bool chargeController_SetStartCharging(enum ChargeSource source)
{
	ESP_LOGW(TAG, "Charging Requested by %i", source);

	bool retValue = false;

	if(source == eCHARGE_SOURCE_RAND_DELAY)
	{
		///Start Timer
		chargeController_SetStartTimer();
		retValue = true;
	}
	else
	{
		retValue = chargeController_SendStartCommandToMCU();
	}

	return retValue;
}


