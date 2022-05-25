#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "chargeController.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "freertos/timers.h"
#include <string.h>
#include "sessionHandler.h"
#include "protocol_task.h"
#include <time.h>
#include "../components/ntp/zntp.h"

static const char *TAG = "CHARGECONTROL  ";


const uint32_t maxStartDelay = 600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
static bool runStartimer = false;
static bool overrideTimer = false;

//static char fullTimeScheduleString[] = {"31:0812:1234;96:2200:2330;03:1130:1245"};
static char fullTimeScheduleString[] = {"02:1150:1200"};
static int nrOfSchedules = 0;
//static char timeScheduleString[13] = {0};

struct TimeSchedule
{
	uint8_t Days;
	int		StartHour;
	int		StartMin;
	int		StopHour;
	int 	StopMin;
};

struct TimeSchedule timeSchedules[10] = {0};

void chargeController_Init()
{
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(1000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);

	chargeController_SetTimes();
}



void chargeController_SetTimes()
{
	int scheduleLen = strlen(fullTimeScheduleString);

	nrOfSchedules = scheduleLen / 12;

	int i = 0;
	for (i = 0; i < nrOfSchedules; i++)
	{
		int base = i*13;
		if((fullTimeScheduleString[base+2] == ':' ) && (fullTimeScheduleString[base+7] == ':'))
		{
			char parseBuf[3] = {0};

			/// Days
			memcpy(parseBuf, &fullTimeScheduleString[base], 2);
			timeSchedules[i].Days = atoi(parseBuf);

			/// StartHour
			memcpy(parseBuf, &fullTimeScheduleString[base+3], 2);
			timeSchedules[i].StartHour = atoi(parseBuf);

			/// StartMin
			memcpy(parseBuf, &fullTimeScheduleString[base+5], 2);
			timeSchedules[i].StartMin = atoi(parseBuf);

			/// StopHour
			memcpy(parseBuf, &fullTimeScheduleString[base+8], 2);
			timeSchedules[i].StopHour = atoi(parseBuf);

			/// StopMin
			memcpy(parseBuf, &fullTimeScheduleString[base+10], 2);
			timeSchedules[i].StopMin = atoi(parseBuf);

			ESP_LOGW(TAG, "#%i: Day: 0x%X Start: %02i:%02i Stop: %02i:%02i", i, timeSchedules[i].Days, timeSchedules[i].StartHour, timeSchedules[i].StartMin, timeSchedules[i].StopHour, timeSchedules[i].StopMin);
		}


	}

}



void chargeController_CancelDelay()
{
	if(runStartimer == true)
	{
		overrideTimer = true;
	}
}

static bool isScheduleActive = true;
void RunStartChargeTimer()
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
	/// Check for charging start
	else if((isScheduleActive == true) && (MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_REQUESTING))
	{
		struct tm updatedTimeStruct = {0};
		zntp_GetTimeStruct(&updatedTimeStruct);

		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{
			if(timeSchedules[scheduleNr].Days & updatedTimeStruct.tm_wday)
			{
				///We have the correct day -> Check hour

				if(timeSchedules[scheduleNr].StartHour == updatedTimeStruct.tm_hour)
				{
					///We have the correct day -> Check hour
					if(timeSchedules[scheduleNr].StartMin == updatedTimeStruct.tm_min)
					{
						ESP_LOGW(TAG, "***** START NOW!!! ******");
						//startSent = true;
					}
					else
					{
						ESP_LOGW(TAG, "Starting in %d min", updatedTimeStruct.tm_min-timeSchedules[scheduleNr].StartMin);
					}
				}
				else
				{
					ESP_LOGW(TAG, "Starting in %d hours %d min", updatedTimeStruct.tm_hour-timeSchedules[scheduleNr].StartHour, updatedTimeStruct.tm_min-timeSchedules[scheduleNr].StartMin);
				}

			}
		}

	}

	else if((isScheduleActive == true) && (MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_PAUSED))
	{

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


bool chargeController_SendStartCommandToMCU()
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


