#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "chargeController.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "freertos/timers.h"
#include <string.h>
#include "sessionHandler.h"
#include "protocol_task.h"
#include "time.h"
#include "../components/ntp/zntp.h"
#include "storage.h"

static const char *TAG = "CHARGECONTROL  ";


const uint32_t maxStartDelay = 10;//600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
static bool runStartimer = false;
static uint8_t overrideTimer = 0;

//static char fullTimeScheduleString[] = {"31:0812:1234;96:2200:2330;03:1130:1245"};
static char fullTimeScheduleString[] = {"031:0800:1200;031:1600:1800"};
static int nrOfSchedules = 0;
static bool enforceScheduleAndDelay = false;

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

static struct TimeSchedule timeSchedules[14] = {0};
static bool isScheduleActive = true;

void chargeController_Init()
{
	ESP_LOGE(TAG, "SETTING TIMER");
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(2000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);


	//DEBUG
	storage_Set_Location("NOR");//"GBR"//
	storage_Set_Timezone("Europe/Oslo");
	storage_Set_TimeSchedule(fullTimeScheduleString);
	if(enforceScheduleAndDelay == false)
	{
		ESP_LOGE(TAG, "****** ENFORCING SCHEDULE AND DELAY *********");
		enforceScheduleAndDelay = true;
		chargeController_SetStandaloneState(storage_Get_Standalone());
		/// Don't need to call storage_SaveConfiguration() here because only MCU will potentially be changed

	}



	if(strlen(fullTimeScheduleString) >= 12)
		isScheduleActive = true;

	else
		isScheduleActive = false;


	ESP_LOGW(TAG, "Schedule: %s, %s, %s is %s", storage_Get_Location(), storage_Get_Timezone(), storage_Get_TimeSchedule(), isScheduleActive ? "ACTIVE" : "INACTIVE");

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
	char * location = storage_Get_Location();
	int daylightSaving = 0;

	if(strncmp("NOR", location, 3) == 0)
	{
		daylightSaving = 2;
	}

	if(strncmp("GBR", location, 3) == 0)
	{
		daylightSaving = 1;
	}

	///Default to UTC + 0
	return daylightSaving;
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

	nrOfSchedules = (scheduleLen + 1) / 14;

	int i = 0;
	for (i = 0; i < nrOfSchedules; i++)
	{
		int base = i*14;
		if((fullTimeScheduleString[base+3] == ':' ) && (fullTimeScheduleString[base+8] == ':'))
		{
			char parseBuf[4] = {0};

			/// Days
			memcpy(parseBuf, &fullTimeScheduleString[base], 3);
			timeSchedules[i].Days = atoi(parseBuf);

			memset(parseBuf, 0, 4);

			/// StartHour
			memcpy(parseBuf, &fullTimeScheduleString[base+4], 2);
			timeSchedules[i].StartHour = atoi(parseBuf);

			/// StartMin
			memcpy(parseBuf, &fullTimeScheduleString[base+6], 2);
			timeSchedules[i].StartMin = atoi(parseBuf);

			timeSchedules[i].StartTotalMinutes = timeSchedules[i].StartHour * 60 + timeSchedules[i].StartMin;

			/// StopHour
			memcpy(parseBuf, &fullTimeScheduleString[base+9], 2);
			timeSchedules[i].StopHour = atoi(parseBuf);

			/// StopMin
			memcpy(parseBuf, &fullTimeScheduleString[base+11], 2);
			timeSchedules[i].StopMin = atoi(parseBuf);

			timeSchedules[i].StopTotalMinutes = timeSchedules[i].StopHour * 60 + timeSchedules[i].StopMin;

			if(timeSchedules[i].StopHour < timeSchedules[i].StartHour)
				timeSchedules[i].StopNextDay = true;
			else
				timeSchedules[i].StopNextDay = false;

			timeSchedules[i].isPaused = false;

			ESP_LOGW(TAG, "#%i: Day: 0x%X Start: %02i:%02i Stop: %02i:%02i", i, timeSchedules[i].Days, timeSchedules[i].StartHour, timeSchedules[i].StartMin, timeSchedules[i].StopHour, timeSchedules[i].StopMin);
		}


	}

}


static int previousOverrideTimer = 0;
void chargeController_Override()
{
		overrideTimer = 1;
}


void chargeController_CancelOverride()
{
		overrideTimer = 0;
}

static struct tm nowTime = {0};
static bool testWithNowTime = false;
void chargeController_SetNowTime(char * timeString)
{
	if(strlen(timeString) == 5)
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
		ESP_LOGW(TAG, "Cleared nowTime or invalid arg!");
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
	//snprintf(startTimeString, sizeof(startTimeString), "0 : %02i:%02iZ", timeSchedules[currentSchedule].StopHour - chargeController_GetLocalTimeOffset(), timeSchedules[currentSchedule].StopMin);
	int startSec = randomStartDelay % 60;			//Remaining seconds
	int addMin = (int)(randomStartDelay / 60);	//Whole 60-seconds rolling over to next minutes

	int allMin = timeSchedules[currentSchedule].StopMin + addMin; //All Schedule and rollover minutes
	int startMin = allMin % 60;

	int allHours =  timeSchedules[currentSchedule].StopHour + (int)(allMin/60);

	int startHours;
	if(allHours < 24)
		startHours = allHours;
	else
		startHours = allHours - 24; //23:55:00 + 600 seconds -> 00:05:00

	int offsetFromUTC = chargeController_GetLocalTimeOffset();

	int UTCStop = timeSchedules[currentSchedule].StopHour - offsetFromUTC;

	/// Check if rolling over midnight backwards
	/*bool isSameDay = false;
	if(UTCStop < 0)
	{
		UTCStop += 24;
		isSameDay = true;
	}*/




	/*int duration;
	if(timeSchedules[currentSchedule].StopTotalMinutes > timeSchedules[currentSchedule].StartTotalMinutes)
	{
		/// Sum of minutes
		duration = timeSchedules[currentSchedule].StopTotalMinutes - timeSchedules[currentSchedule].StartTotalMinutes;
	}
	else
	{
		/// Sum of minutes before and after midnight
		duration = 1440 - timeSchedules[currentSchedule].StartTotalMinutes + timeSchedules[currentSchedule].StopTotalMinutes;
	}*/

	//Make UTC time string for the same day
	time_t nowSecUTC = 0;
	struct tm timeinfo = { 0 };

	time(&nowSecUTC);
	//int nowUTC = now - (3600 * offsetFromUTC) + ;
	localtime_r(&nowSecUTC, &timeinfo);

	//time_t inTWentyFourHours = nowUTCSec + 86400;
	//struct tm midnight =

	timeinfo.tm_hour = 23;
	timeinfo.tm_min = 59;
	timeinfo.tm_sec = 59;

	time_t secAtMidnightUTC = mktime(&timeinfo) + 1 - (3600 * offsetFromUTC);

	timeinfo.tm_hour = timeSchedules[currentSchedule].StopHour;
	timeinfo.tm_min = timeSchedules[currentSchedule].StopMin;
	timeinfo.tm_sec = 0;

	time_t secAtStop = mktime(&timeinfo);

	time_t secAtUTCStop = secAtStop - (3600 * offsetFromUTC);

	bool stopOnNextDayUTC = false;

	///Check for StopAtNextDay UTC
	if((timeSchedules[currentSchedule].StopHour - offsetFromUTC) < (timeSchedules[currentSchedule].StartHour - offsetFromUTC))
	{
		stopOnNextDayUTC = true;
		ESP_LOGE(TAG, "1: stopOnNextDayUTC = true");
	}

	///Check if we are not nextDay UTC

	bool isNextDayUTC = false;
	///Check if current UTC hour is smaller than Stop UTC hour
	if((timeinfo.tm_hour - offsetFromUTC) < (timeSchedules[currentSchedule].StopHour - offsetFromUTC))
	{
		isNextDayUTC = true;
		ESP_LOGE(TAG, "2: isNextDayUTC = true");
	}

	if((stopOnNextDayUTC == true) && (isNextDayUTC == true))
	{
		///Stop occur on this date
		ESP_LOGE(TAG, "3: We are on stop day, don't add");
	}
	else if((stopOnNextDayUTC == true) && (isNextDayUTC == false))
	{
		secAtUTCStop += 86400;
		ESP_LOGE(TAG, "4: Stop tomorrow, add one day");

	}
	else if (stopOnNextDayUTC == false)
	{
		ESP_LOGE(TAG, "5: Stop on same day, don't add");
	}

	///Add the random delay to the stop time before converting to tm-struct time
	secAtUTCStop += randomStartDelay;



	///StopTime
	/*if(nowSecUTC < secAtMidnightUTC )
	{
		ESP_LOGE(TAG, "1 We are on stop day ");
		if(secAtUTCStop > secAtMidnightUTC)
		{
			secAtUTCStop += 86400;
			ESP_LOGE(TAG, "2 Start next day");
		}
	}
	else
	{
		//secAtUTCStop += 86400;
		ESP_LOGE(TAG, "3 We are on next day, don't add");
	}*/


	//localtime_r(&nowUTCSec, &inTWentyFourHours);

	///Construct the local stop time tm
	//struct tm localStopTime;

	//timeinfoUTC.tm_year =

	//localStopTime.tm_hour = timeSchedules[currentSchedule].stopHour;
	//localStopTime.tm_min = timeSchedules[currentSchedule].stopMin;

	//time_t localStopTimeSec = mktime(&localStopTime);


	localtime_r(&secAtUTCStop, &timeinfo);

	char strftime_buf[64] = {0};

	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%02m-%02dT%02H:%02M:%02SZ", &timeinfo);

	strcpy(startTimeString, strftime_buf);

	ESP_LOGW(TAG, "************** START TIME UTC: %s ****************", startTimeString);

	hasNewStartTime = true;
}

static void chargeController_ClearNextStartTime()
{
	strcpy(startTimeString, "");
	hasNewStartTime = true;
}

bool chargeController_CheckForNewScheduleEvent()
{
	return hasNewStartTime;
}

char * chargeController_GetNextStartString()
{
	hasNewStartTime = false;
	return startTimeString;
}


bool chargeController_IsScheduleActive()
{
	return isScheduleActive;
}

static bool hasBeenDisconnected = false;
void chargeController_SetHasBeenDisconnected()
{
	hasBeenDisconnected = true;
}

//#include "zones.h"
//#include "../components/micro_tz_db/zones.h"
/*#include "../components/utz/utz.h"
#include "../components/utz/zones.h"
*/
static uint16_t isPausedByAnySchedule = 0x0000;
static uint16_t previousIsPausedByAnySchedule = 0x0000;
static char scheduleString[150] = {0};

void RunStartChargeTimer()
{
	/*printf("Total library db size: %d B\n", sizeof(zone_rules) + sizeof(zone_abrevs) + sizeof(zone_defns) + sizeof(zone_names));

	udatetime_t dt = {0};
	dt.date.year = 17;
	dt.date.month = 9;
	dt.date.dayofmonth = 26;
	dt.time.hour = 1;
	dt.time.minute = 0;
	dt.time.second = 0;

	uzone_t active_zone;
	get_zone_by_name("Europe/Oslo", active_zone);
	uoffset_t offset;
	char c = get_current_offset(&active_zone, &dt, &offset);
	printf("%s, current offset: %d.%d\n", active_zone.name, offset.hours, offset.minutes / 60);
	printf(active_zone.abrev_formatter, c);
	printf("\n");*/

	/*time_t now;
	time(&now);
	struct tm timeinfo;
	char strftime_buf[64];
	const char * posix_str = micro_tz_db_get_posix_str("Europe/Oslo");
	ESP_LOGE(TAG, "********************** TIME LIB STRING %s ************************", posix_str);
*/
    /*setenv("TZ", posix_str, 1);
    tzset();

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    printf("The current date/time is: %s\n", strftime_buf);
*/
	enum ChargerOperatingMode opMode = MCU_GetChargeOperatingMode();

	/// This check is needed to ensure NextStartTime is communicated and cleared correctly
	if(hasBeenDisconnected == true)
	{
		ESP_LOGE(TAG, "**** DISCONNECTED: Clearing all paused-flags ****");
		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{
			if(timeSchedules[scheduleNr].isPaused == true)
			{
				ESP_LOGE(TAG, "******* SETTING TO FALSE *********");
				timeSchedules[scheduleNr].isPaused = false;
			}
		}

		hasBeenDisconnected = false;
		chargeController_ClearNextStartTime();
	}


	if(isScheduleActive == true)
	{

		if((randomStartDelay > 0) || (overrideTimer > 0))
		{
			/// Check if we are in active or passive time interval

			struct tm updatedTimeStruct = {0};
			zntp_GetLocalTimeZoneStruct(&updatedTimeStruct, 3600 * 2);

			// Enable for testing
			if(testWithNowTime)
			{
				updatedTimeStruct.tm_wday = nowTime.tm_wday;
				updatedTimeStruct.tm_hour = nowTime.tm_hour;
				updatedTimeStruct.tm_min = nowTime.tm_min;
				updatedTimeStruct.tm_sec = 00;
			}

			int minutesNow = updatedTimeStruct.tm_hour * 60 + updatedTimeStruct.tm_min;

			strcpy(scheduleString, "");

			int scheduleNr;
			for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
			{

				///When ending override, clear all paused schedules to be able to re-pause and reset nextStopTime
				if((overrideTimer == 0) && (previousOverrideTimer > 0))
				{
					timeSchedules[scheduleNr].isPaused = false;
				}

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
						{
							chargeController_SetNextStartTime(updatedTimeStruct.tm_wday, scheduleNr);

							/// Make sure the randomStartDelay is reset for each schedule start
							startDelayCounter = randomStartDelay;
						}

						timeSchedules[scheduleNr].isPaused= true;

						int localTimeOffset = chargeController_GetLocalTimeOffset();
						snprintf(scheduleString, sizeof(scheduleString), "Within pause interval [%02i:%02i] -> %i %02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%i (UTC+%i) (1)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin,    (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, randomStartDelay, localTimeOffset);
					}

					/// Stop on next day							2300-1380											1400			0100-60								0200-120
					else if(timeSchedules[scheduleNr].StopNextDay && ((timeSchedules[scheduleNr].StartTotalMinutes <= minutesNow) || (minutesNow < timeSchedules[scheduleNr].StopTotalMinutes)))
					{
						if(timeSchedules[scheduleNr].isPaused == false)
						{
							/// Handle rollover 6->0
							if(updatedTimeStruct.tm_wday == 6)
								chargeController_SetNextStartTime(0, scheduleNr);
							else
								chargeController_SetNextStartTime(updatedTimeStruct.tm_wday+1, scheduleNr);


							/// Make sure the randomStartDelay is reset for each schedule start
							startDelayCounter = randomStartDelay;
						}

						timeSchedules[scheduleNr].isPaused= true;

						int localTimeOffset = chargeController_GetLocalTimeOffset();

						if((timeSchedules[scheduleNr].StartTotalMinutes <= minutesNow) && (minutesNow < 1440))
						{
							int remainingMinutes = (1440 - minutesNow) + timeSchedules[scheduleNr].StopTotalMinutes;
							snprintf(scheduleString, sizeof(scheduleString), "Within pause interval #%i: [%02i:%02i] ->> %i %02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%i (UTC+%i) (2)", scheduleNr, timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin,    (int)(remainingMinutes/60), (remainingMinutes) % 60, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, randomStartDelay, localTimeOffset);
						}
						else
						{
							snprintf(scheduleString, sizeof(scheduleString), "Within pause interval #%i: [%02i:%02i] ->> %i %02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%i (UTC+%i) (3)", scheduleNr, timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin,    (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, randomStartDelay, localTimeOffset);
						}
					}

					/// WE ARE IN THE ALLOW CHARGING STATE
					else
					{
						timeSchedules[scheduleNr].isPaused = false;

						/*if(minutesNow < timeSchedules[scheduleNr].StartTotalMinutes)
							//ESP_LOGW(TAG, "Before pause interval %i %02i:%02i -> [%02i:%02i] - [%02i:%02i] (4)", updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min,    timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin);
							snprintf(scheduleString, sizeof(scheduleString), "Before pause interval %i %02i:%02i -> [%02i:%02i] - [%02i:%02i] (4)", updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min,    timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin);
						else if(minutesNow >= timeSchedules[scheduleNr].StopTotalMinutes)
							//ESP_LOGW(TAG, "After pause interval [%02i:%02i] - [%02i:%02i] -> %i %02i:%02i (5)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min);
							snprintf(scheduleString, sizeof(scheduleString), "After pause interval [%02i:%02i] - [%02i:%02i] -> %i %02i:%02i (5)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min);
							*/
					}

				}
				else
				{
					if(timeSchedules[scheduleNr].isPaused == true)
					{
						ESP_LOGW(TAG, "No schedule for today - %i - Allowing charging", updatedTimeStruct.tm_wday);
						timeSchedules[scheduleNr].isPaused = false;
					}
				}

				if(timeSchedules[scheduleNr].isPaused)
					isPausedByAnySchedule |= (0x1 << scheduleNr);
				else
					isPausedByAnySchedule &= ~(0x1 << scheduleNr);


				/*if(timeSchedules[scheduleNr].isPaused == false)
				{
					startTimeString[0] = '\0';
					hasNewStartTime = false;
				}*/
			}

			previousOverrideTimer = overrideTimer;

			/// If overriding by Cloud or BLE, replace the value of isPauseByAnySchedule
			if((isPausedByAnySchedule > 0) && (overrideTimer > 0))
					isPausedByAnySchedule = 0;
			else if((isPausedByAnySchedule == 0) && (overrideTimer > 0))
			{
				overrideTimer = 0;
				ESP_LOGW(TAG, "***** Stopped OVERRIDE ******");
			}



			if(isPausedByAnySchedule == 0)// || (overrideTimer == true))
			{
				snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " ACTIVE (Pb: 0x%04X) Ov: %i", isPausedByAnySchedule, overrideTimer);
				if(previousIsPausedByAnySchedule > 0)
				{
					previousIsPausedByAnySchedule = 0;
				}
				else
				{
					//connectedWhileActive = true;
					if(startDelayCounter >= 1)
						startDelayCounter = 1;
				}

				chargeController_StartWithRandomDelay();

				//Clear info variables
				startTimeString[0] = '\0';
				hasNewStartTime = false;

			}
			else
			{
				snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " PAUSED (Pb: 0x%04X) Ov: %i", isPausedByAnySchedule, overrideTimer);

				// /When schedule is paused, but MCU is active (e.g. ESP restart), then pause the MCU
				if((opMode == CHARGE_OPERATION_STATE_CHARGING) || ((opMode == CHARGE_OPERATION_STATE_PAUSED) && (GetFinalStopActiveStatus() == false)))
				{
					ESP_LOGE(TAG, "***** MCU active: %i - pausing it ******", opMode);

					//502
					MessageType ret = MCU_SendCommandId(CommandStopChargingFinal);
					if(ret == MsgCommandAck)
					{
						ESP_LOGI(TAG, "MCU CommandStopChargingFinal command OK");
						SetFinalStopActiveStatus(1);
					}
					else
					{
						ESP_LOGE(TAG, "MCU CommandStopChargingFinal command FAILED");
					}
				}
			}

			ESP_LOGW(TAG, "%i: %s", strlen(scheduleString), scheduleString);
		}
		else
		{
			/*ESP_LOGE(TAG, "Schedule conditions not met, clearing all paused-flags");
			int scheduleNr;
			for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
			{
				if(timeSchedules[scheduleNr].isPaused == true)
				{
					ESP_LOGE(TAG, "******* SETTING TO FALSE *********");
					timeSchedules[scheduleNr].isPaused = false;
				}
			}*/

		}

	}

	if((storage_Get_Standalone() == 1) && (opMode == CHARGE_OPERATION_STATE_REQUESTING))
	{
		chargeController_SendStartCommandToMCU();
	}

}

void chargeController_StartWithRandomDelay()
{
	if((startDelayCounter > 0) || (overrideTimer == 1))
	{
		startDelayCounter--;

		//if(startDelayCounter % 5 == 0)
			ESP_LOGE(TAG, "RandomDelayCounter %i/%i, Override: %i", startDelayCounter, randomStartDelay, overrideTimer);

		if((startDelayCounter == 0) || (overrideTimer == 1))
		{

			if(overrideTimer == 1)
			{
				ESP_LOGW(TAG, "Starting due to OVERRIDE!");
				overrideTimer = 2;
			}

			chargeController_ClearNextStartTime();
			chargeController_SendStartCommandToMCU();

			runStartimer = false;
			//overrideTimer = false;
			startDelayCounter = 0;
			//connectedWhileActive = false;
		}
	}
}


void chargeController_SetRandomStartDelay()
{
	/// Formula: int randomStartDelay = (esp_random() % (high - low + 1)) + low;
	randomStartDelay = (esp_random() % (maxStartDelay- 1 + 1) + 1);
	startDelayCounter = randomStartDelay;

	ESP_LOGE(TAG, "********* StartDelayCounter set to %i **************", startDelayCounter);

	//runStartimer = true;
}

void chargeController_ClearRandomStartDelay()
{
	randomStartDelay = 0;
}


bool chargeController_SendStartCommandToMCU()
{
	bool retval = false;

	enum ChargerOperatingMode chOpMode = MCU_GetChargeOperatingMode();
	if((chOpMode == CHARGE_OPERATION_STATE_REQUESTING) && (storage_Get_Standalone() == 1))
	{
		ESP_LOGW(TAG, "********* Starting from state CHARGE_OPERATION_STATE_REQUESTING **************");

		MessageType ret = MCU_SendCommandId(CommandStartCharging);
		if(ret == MsgCommandAck)
		{
			ESP_LOGW(TAG, "Sent start Command to MCU OK");
			retval =  true;
		}
		else
		{
			ESP_LOGW(TAG, "Sent start Command to MCU FAILED");
			retval =  false;
		}

	}
	if((chOpMode == CHARGE_OPERATION_STATE_REQUESTING) && (storage_Get_Standalone() == 0))
		{
			ESP_LOGW(TAG, "********* Starting from state CHARGE_OPERATION_STATE_REQUESTING **************");

			MessageType ret = MCU_SendCommandId(CommandStartCharging);
			if(ret == MsgCommandAck)
			{
				ESP_LOGW(TAG, "Sent start Command to MCU OK");
				retval =  true;
			}
			else
			{
				ESP_LOGW(TAG, "Sent start Command to MCU FAILED");
				retval =  false;
			}

		}
	else if((chOpMode == CHARGE_OPERATION_STATE_PAUSED))// && (GetFinalStopActiveStatus() == true))
	{
		ESP_LOGW(TAG, "********* Resuming from state CHARGE_OPERATION_STATE_PAUSED && FinalStop == true **************");

		MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);// = 509
		if(ret == MsgCommandAck)
		{

			ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK");
			SetFinalStopActiveStatus(0);
			retval = true;
		}
		else
		{
			ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command FAILED");
			retval = false;
		}
	}
	else
	{
		ESP_LOGW(TAG, "********* OTHER STATE: %i **************", chOpMode);
	}

	return retval;
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


/*
 * Ensure storage_SaveConfiguration() is called when this function returns true
 */
bool chargeController_SetStandaloneState(uint8_t isStandalone)
{
	MessageType ret;
	if(enforceScheduleAndDelay == true)
		ret = MCU_SendUint8Parameter(ParamIsStandalone, 0); 	//MCU must be controlled by ESP due to schedule function
	else
		ret = MCU_SendUint8Parameter(ParamIsStandalone, (uint8_t)isStandalone);

	if(ret == MsgWriteAck)
	{
		storage_Set_Standalone((uint8_t)isStandalone);
		ESP_LOGI(TAG, "DoSave 712 standalone=%d\n", isStandalone);
		return true;
	}
	else
	{
		ESP_LOGE(TAG, "MCU standalone parameter error");
		return false;
	}

}
