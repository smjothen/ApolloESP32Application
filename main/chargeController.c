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
#include "storage.h"

static const char *TAG = "CHARGECONTROL  ";


const uint32_t maxStartDelay = 600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
static bool runStartimer = false;
static bool overrideTimer = false;

//static char fullTimeScheduleString[] = {"31:0812:1234;96:2200:2330;03:1130:1245"};
static char fullTimeScheduleString[] = {"31:0800:1200"};
static int nrOfSchedules = 0;
//static char timeScheduleString[13] = {0};

struct TimeSchedule
{
	uint8_t Days;
	int		StartHour;
	int		StartMin;
	int		StartTotalMinutes;
	int		StopHour;
	int 	StopMin;
	int		StopTotalMinutes;
	bool 	StopNextDay;
	bool	isPaused;
};

struct TimeSchedule timeSchedules[10] = {0};

void chargeController_Init()
{
	ESP_LOGE(TAG, "SETTING TIMER");
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(3000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);

	chargeController_SetTimes();

	/*ESP_LOGW(TAG, " #### SizeOf time_t: %i", sizeof(time_t));

	struct tm tmUpdatedTime;

	time_t nowInSeconds = 0xF0000001;

	//time(&nowInSeconds);
	localtime_r(&nowInSeconds, &tmUpdatedTime);

	ESP_LOGW(TAG, " #### Date: %i:%i:%i", tmUpdatedTime.tm_year, tmUpdatedTime.tm_mon, tmUpdatedTime.tm_mday);
*/
}


int chargeController_GetLocalTimeOffset()
{
	char * location = "NOR";//"GBR"//storage_Get_Location();
	int daylightSaving = 2;

	if(strncmp("NOR", location, 3) == 0)
	{
		return daylightSaving;
	}

	///Default to UTC + 0
	return 0;
}

void chargeController_WriteNewTimeSchedule(char * timeSchedule)
{
	//Write and read back for confirmation
	storage_Set_TimeSchedule(timeSchedule);
	char * ts = storage_Get_TimeSchedule();
	strcpy(fullTimeScheduleString, ts);

	ESP_LOGW(TAG, "Setting timeSchedule: %s -> %s", timeSchedule, fullTimeScheduleString);
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

			timeSchedules[i].StartTotalMinutes = timeSchedules[i].StartHour * 60 + timeSchedules[i].StartMin;

			/// StopHour
			memcpy(parseBuf, &fullTimeScheduleString[base+8], 2);
			timeSchedules[i].StopHour = atoi(parseBuf);

			/// StopMin
			memcpy(parseBuf, &fullTimeScheduleString[base+10], 2);
			timeSchedules[i].StopMin = atoi(parseBuf);

			timeSchedules[i].StopTotalMinutes = timeSchedules[i].StopHour * 60 + timeSchedules[i].StopMin;

			if(timeSchedules[i].StopHour < timeSchedules[i].StartHour)
				timeSchedules[i].StopNextDay = true;
			else
				timeSchedules[i].StopNextDay = false;

			timeSchedules[i].isPaused = true;

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

static struct tm nowTime = {0};
static bool testWithNowTime = false;
void chargeController_SetNowTime(char * timeString)
{
	if (timeString[0] != '\0')
	{
		int intStr = atoi(timeString);
		nowTime.tm_wday = intStr/10000;

		nowTime.tm_min = atoi(&timeString[3]);
		timeString[3] = '\0';

		nowTime.tm_hour = atoi(&timeString[1]);

		testWithNowTime = true;
		ESP_LOGW(TAG, "Test nowTime: %id %02i:%02i",nowTime.tm_wday, nowTime.tm_hour, nowTime.tm_min);
	}
	else
	{
		testWithNowTime = false;
		ESP_LOGW(TAG, "Cleared nowTime!");
	}

}

char * GetDayAsString(int dayNr)
{
	if(dayNr == 0)
		return "Sunday";
	else if(dayNr == 1)
		return "Monday";
	else if(dayNr == 2)
		return "Tuesday";
	else if(dayNr == 3)
		return "Wednesday";
	else if(dayNr == 4)
		return "Thursday";
	else if(dayNr == 5)
		return "Friday";
	else if(dayNr == 6)
		return "Saturday";
	else
		return "";
}

static char startTimeString[32] = {0};
static bool hasNewStartTime = false;
static void chargeController_SetNextStartTime(int dayNr, int currentSchedule)
{
	snprintf(startTimeString, sizeof(startTimeString), "Start: %s %02i:%02i ", GetDayAsString(dayNr), timeSchedules[currentSchedule].StopHour, timeSchedules[currentSchedule].StopMin);
	hasNewStartTime = true;
}

static void chargeController_ClearNextStartTime()
{
	snprintf(startTimeString, sizeof(startTimeString), "Active");
	hasNewStartTime = true;
}

char * chargeController_GetNextStartString()
{
	hasNewStartTime = false;
	return startTimeString;
}

static bool isScheduleActive = true;
static uint16_t isPausedByAnySchedule = 0x0000;
static uint16_t previousIsPausedByAnySchedule = 0x0000;
static char scheduleInfo[50] = {0};
static char scheduleString[150] = {0};
void RunStartChargeTimer()
{
	if((startDelayCounter > 0))// && (runStartimer == true))
	{
		startDelayCounter--;

		if(startDelayCounter % 5 == 0)
			ESP_LOGW(TAG, "startDelayCounter %i/%i, Override: %i", startDelayCounter, randomStartDelay, overrideTimer);

		if((startDelayCounter == 0) || (overrideTimer == true))
		{
			chargeController_SendStartCommandToMCU();

			runStartimer = false;
			overrideTimer = false;
			startDelayCounter = 0;
		}
	}
	if(isScheduleActive == true)
	{
		/// Check if we are in active or passive time interval

		struct tm updatedTimeStruct = {0};
		zntp_GetTimeStruct(&updatedTimeStruct);

		// Enable for testing
		if(testWithNowTime)
		{
			updatedTimeStruct.tm_wday = nowTime.tm_wday;
			updatedTimeStruct.tm_hour = nowTime.tm_hour;
			updatedTimeStruct.tm_min = nowTime.tm_min;
			updatedTimeStruct.tm_sec = 00;
		}


		int minutesNow = updatedTimeStruct.tm_hour * 60 + updatedTimeStruct.tm_min;

		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{
			uint8_t stopdays = 0;
			if(timeSchedules[scheduleNr].StopNextDay)
			{
				///Bitshift to get stopday byte
				stopdays = timeSchedules[scheduleNr].Days << 1;

				/// Check for overflow - move bit 7 -> bit 0
				if(stopdays & 0x80)
				{
					stopdays += 1;
					stopdays &= ~0x80;
				}
			}

			/// On the same day
			uint8_t shiftPositions = 0;
			if(updatedTimeStruct.tm_wday == 0)
				shiftPositions = 6;
			else
				shiftPositions = updatedTimeStruct.tm_wday;

			volatile bool isPauseDay = (timeSchedules[scheduleNr].Days & (0x01 << (shiftPositions - 1)));

			/// StopNextDay					110									2
			volatile bool isTheNextResumeDay= (stopdays & (0x01 << (shiftPositions - 1)));

			/// Check if schedule is applicable
			if((isPauseDay || isTheNextResumeDay))
			{
				/// ARE WE INSIDE PAUSE INTERVAL?

				/// Stop on same day
				if(!timeSchedules[scheduleNr].StopNextDay && (minutesNow >= timeSchedules[scheduleNr].StartTotalMinutes) && (minutesNow < timeSchedules[scheduleNr].StopTotalMinutes))
				{
					if(timeSchedules[scheduleNr].isPaused== false)
						chargeController_SetNextStartTime(updatedTimeStruct.tm_wday, scheduleNr);

					timeSchedules[scheduleNr].isPaused= true;

					int localTimeOffset = chargeController_GetLocalTimeOffset();
					snprintf(scheduleInfo, sizeof(scheduleInfo), "%02i hours %02i min at %02i:%02i (UTC+%i) (1)", (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour + localTimeOffset, timeSchedules[scheduleNr].StopMin, localTimeOffset);

					//ESP_LOGW(TAG, "Within pause interval [%02i:%02i] -> %i %02i:%02i -> [%02i:%02i]. Resuming in %s", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, scheduleInfo);
					snprintf(scheduleString, sizeof(scheduleString), "Within pause interval [%02i:%02i] -> %i %02i:%02i -> [%02i:%02i]. Resuming in %s", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, scheduleInfo);
				}

				/// Stop on next day							2300-1380											1400			0100-60								0200-120
				else if(timeSchedules[scheduleNr].StopNextDay && ((timeSchedules[scheduleNr].StartTotalMinutes <= minutesNow) || (minutesNow < timeSchedules[scheduleNr].StopTotalMinutes)))
				{
					if(timeSchedules[scheduleNr].isPaused== false)
					{
						/// Handle rollover 6->0
						if(updatedTimeStruct.tm_wday == 6)
							chargeController_SetNextStartTime(0, scheduleNr);
						else
							chargeController_SetNextStartTime(updatedTimeStruct.tm_wday+1, scheduleNr);
					}

					timeSchedules[scheduleNr].isPaused= true;

					int localTimeOffset = chargeController_GetLocalTimeOffset();

					if((timeSchedules[scheduleNr].StartTotalMinutes <= minutesNow) && (minutesNow < 1440))
					{
						int remainingMinutes = (1440 - minutesNow) + timeSchedules[scheduleNr].StopTotalMinutes;

						snprintf(scheduleInfo, sizeof(scheduleInfo), "%02i hours %02i min at %02i:%02i (UTC+%i) (2)", (int)(remainingMinutes/60), (remainingMinutes) % 60, timeSchedules[scheduleNr].StopHour + localTimeOffset, timeSchedules[scheduleNr].StopMin, localTimeOffset);
					}
					else
					{
						snprintf(scheduleInfo, sizeof(scheduleInfo), "%02i hours %02i min at %02i:%02i (UTC+%i) (3)", (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour + localTimeOffset, timeSchedules[scheduleNr].StopMin, localTimeOffset);
					}

					//ESP_LOGW(TAG, "Within pause interval [%02i:%02i] ->> %i %02i:%02i -> [%02i:%02i]. Resuming in %s", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, scheduleInfo);
					snprintf(scheduleString, sizeof(scheduleString), "Within pause interval [%02i:%02i] ->> %i %02i:%02i -> [%02i:%02i]. Resuming in %s", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, scheduleInfo);
				}

				/// WE ARE IN THE ALLOW CHARGING STATE
				else
				{
					timeSchedules[scheduleNr].isPaused= false;

					if(minutesNow < timeSchedules[scheduleNr].StartTotalMinutes)
						//ESP_LOGW(TAG, "Before pause interval %i %02i:%02i -> [%02i:%02i] - [%02i:%02i] (4)", updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min,    timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin);
						snprintf(scheduleString, sizeof(scheduleString), "Before pause interval %i %02i:%02i -> [%02i:%02i] - [%02i:%02i] (4)", updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min,    timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin);
					else if(minutesNow > timeSchedules[scheduleNr].StopTotalMinutes)
						//ESP_LOGW(TAG, "After pause interval [%02i:%02i] - [%02i:%02i] -> %i %02i:%02i (5)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min);
						snprintf(scheduleString, sizeof(scheduleString), "After pause interval [%02i:%02i] - [%02i:%02i] -> %i %02i:%02i (5)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min);

				}

			}
			else
			{
				if(timeSchedules[scheduleNr].isPaused == true)
				{
					ESP_LOGW(TAG, "No schedule for today - %i - Allowing charging", updatedTimeStruct.tm_wday);
					timeSchedules[scheduleNr].isPaused= false;
				}
			}

			if(timeSchedules[scheduleNr].isPaused)
				isPausedByAnySchedule |= (0x1 << scheduleNr);
			else
				isPausedByAnySchedule &= ~(0x1 << scheduleNr);


			if(timeSchedules[scheduleNr].isPaused == false)
			{
				startTimeString[0] = '\0';
				hasNewStartTime = false;
			}
		}

		if(isPausedByAnySchedule == 0)
		{
			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " ACTIVE (Pb: 0x%04X)", isPausedByAnySchedule);
			if(previousIsPausedByAnySchedule > 0)
			{
				previousIsPausedByAnySchedule = 0;
				chargeController_ClearNextStartTime();
			}
		}
		else
		{
			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " PAUSED (Pb: 0x%04X)", isPausedByAnySchedule);
		}
	}

	ESP_LOGW(TAG, "%i: %s", strlen(scheduleString), scheduleString);

	/// We now know if the charger should be active or not, now check state and PAUSE or RESUME if necessary

	if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_REQUESTING)
	{

	}
	else if(MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_PAUSED)
	{

	}
}


void chargeController_SetRandomStartDelay()
{
	/// Formula: int randomStartDelay = (esp_random() % (high - low + 1)) + low;
	randomStartDelay = (esp_random() % (maxStartDelay + 1));
	startDelayCounter = randomStartDelay;

	ESP_LOGW(TAG, "startDelayCounter set to %i", startDelayCounter);

	//runStartimer = true;
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

	if(source == eCHARGE_SOURCE_SCHEDULE)
	{
		///Start Timer
		//chargeController_SetStartTimer();
		retValue = true;
	}
	else
	{
		retValue = chargeController_SendStartCommandToMCU();
	}

	return retValue;
}


