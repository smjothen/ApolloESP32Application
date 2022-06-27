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
#include "zaptec_cloud_observations.h"

static const char *TAG = "CHARGECONTROL  ";


const uint32_t maxStartDelay = 10;//600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
static bool runStartimer = false;
static uint8_t overrideTimer = 0;

//static char fullTimeScheduleString[] = {"31:0812:1234;96:2200:2330;03:1130:1245"};
//static char fullTimeScheduleString[] = {"031:0800:1200;031:2300:0000;031:0000:0300"};
//static char fullTimeScheduleString[] = {"031:0800:1200;031:1600:1800"};
static char fullTimeScheduleString[SCHEDULE_SIZE] = {0};
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
	bool 	SpanToNextDay;
	bool	isPaused;
};

static struct TimeSchedule timeSchedules[14] = {0};
static bool isScheduleActive = false;

void chargeController_Init()
{
	ESP_LOGE(TAG, "SETTING TIMER");
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(2000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);

	chargeController_Activation();
}

void chargeController_Activation()
{
	//DEBUG
	//storage_Set_Location("NOR");//"GBR"//
	//storage_Set_Timezone("Europe/Oslo");
	//storage_Set_TimeSchedule(fullTimeScheduleString);

	if((strncmp(storage_Get_Location(), "GBR", 3) == 0) && (strlen(storage_Get_TimeSchedule()) >= 13))
	{
		ESP_LOGE(TAG, "****** ENFORCING SCHEDULE AND DELAY *********");
		enforceScheduleAndDelay = true;
		chargeController_SetStandaloneState(storage_Get_Standalone());
		/// Don't need to call storage_SaveConfiguration() here because only MCU will potentially be changed
		chargeController_SetTimes();
		isScheduleActive = true;
	}
	else
	{
		enforceScheduleAndDelay = false;
		chargeController_SetStandaloneState(storage_Get_Standalone());
		isScheduleActive = false;
	}

	ESP_LOGW(TAG, "Schedule: %s, %s, %s, SCHEDULE %s", storage_Get_Location(), storage_Get_Timezone(), storage_Get_TimeSchedule(), isScheduleActive ? "ON" : "OFF");
}

int chargeController_GetLocalTimeOffset()
{
	char * timezone = storage_Get_Timezone();
	int timeZoneOffset = 0;

	if(strcmp("Europe/Oslo", timezone) == 0)
	{
		timeZoneOffset = 2;
	}

	if(strcmp("Europe/London", timezone) == 0)
	{
		timeZoneOffset = 1;
	}

	///Default to UTC + 0
	return timeZoneOffset;
}

void chargeController_WriteNewTimeSchedule(char * timeSchedule)
{
	storage_Set_TimeSchedule(timeSchedule);

	ESP_LOGW(TAG, "Set timeSchedule from Cloud: %s", storage_Get_TimeSchedule());
}

void chargeController_SetTimes()
{
	char * schedule = storage_Get_TimeSchedule();

	if(schedule[0] == '\0')
		return;

	int scheduleLen = strlen(schedule);

	if((scheduleLen < 13) || (scheduleLen > SCHEDULE_SIZE))
		return;

	strcpy(fullTimeScheduleString, schedule);

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

			//Make stop time of 00:00 look like 24:00 to create max end time
			if((timeSchedules[i].StopHour == 0) && (timeSchedules[i].StopMin == 0))
				timeSchedules[i].StopHour = 24;

			timeSchedules[i].StopTotalMinutes = timeSchedules[i].StopHour * 60 + timeSchedules[i].StopMin;

			if(i > 0)
			{
				//If once schedule stops at 00:00 and nest start at 00:00, then set continuous schedule flag in first schedule
				if((timeSchedules[i].StartTotalMinutes == 0) && (timeSchedules[i-1].StopTotalMinutes == 1440))
				{
					timeSchedules[i-1].SpanToNextDay = true;
					ESP_LOGW(TAG, "Schedule %i spans to next day", i-1);
				}

			}

			timeSchedules[i].isPaused = false;

			ESP_LOGW(TAG, "#%i: Day: 0x%X Start: %02i:%02i Stop: %02i:%02i", i, timeSchedules[i].Days, timeSchedules[i].StartHour, timeSchedules[i].StartMin, timeSchedules[i].StopHour, timeSchedules[i].StopMin);
		}


	}
	startDelayCounter = 0;
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

/*char * GetDayAsString(int dayNr)
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
}*/

static char startTimeString[32] = {0};
static bool hasNewStartTime = false;
static void chargeController_SetNextStartTime(int dayNr, int currentSchedule, bool isNextDayPauseDay)
{
	int stopSchedule = currentSchedule;

	if((timeSchedules[currentSchedule].SpanToNextDay == true) && (isNextDayPauseDay == true))
		stopSchedule = currentSchedule + 1;

	int offsetFromUTC = chargeController_GetLocalTimeOffset();

	/// Make UTC time string for the same day
	time_t nowSecUTC = 0;
	struct tm timeinfo = { 0 };

	/// NowTime for comparison
	time(&nowSecUTC);
	localtime_r(&nowSecUTC, &timeinfo);

	if(testWithNowTime == true)
	{
		timeinfo.tm_wday = nowTime.tm_wday;
		timeinfo.tm_mday = timeinfo.tm_mday + (nowTime.tm_wday-2);
		timeinfo.tm_hour = nowTime.tm_hour;
		timeinfo.tm_min = nowTime.tm_min;
		timeinfo.tm_sec = 00;
		nowSecUTC = mktime(&timeinfo);
	}

	/// Midnight for comparison
	timeinfo.tm_hour = 23;
	timeinfo.tm_min = 59;
	timeinfo.tm_sec = 59;
	time_t secAtMidnightUTC = mktime(&timeinfo) + 1;

	/// Stoptime for comparision
	timeinfo.tm_hour = timeSchedules[stopSchedule].StopHour;
	timeinfo.tm_min = timeSchedules[stopSchedule].StopMin;
	timeinfo.tm_sec = 0;
	time_t secAtStopLocal = mktime(&timeinfo);

	time_t secAtStopUTC = secAtStopLocal - (3600 * offsetFromUTC);


	/// Use NowTime, Midnight time and StopTime to determine end of pause interval in UTC

	//To low sec - must be next day
	if(secAtStopUTC < nowSecUTC)
	{
		secAtStopUTC += 86400;
		ESP_LOGW(TAG, "***** 1. Tomorrow *****");
	}
	//Within the remaining day
	else if((secAtStopUTC >= nowSecUTC) && (secAtStopUTC < secAtMidnightUTC))
	{
		ESP_LOGW(TAG, "***** 2. Today *****");
	}
	//Tomorrow
	else if (secAtStopUTC >= secAtMidnightUTC)
	{
		secAtStopUTC += 86400;
		ESP_LOGW(TAG, "***** 3. Tomorrow *****");
	}
	else
	{
		ESP_LOGW(TAG, "***** 4. UNDEFINED *****");
	}


	///Add the random delay to the stop time before converting to tm-struct time
	secAtStopUTC += randomStartDelay;

	localtime_r(&secAtStopUTC, &timeinfo);

	char strftime_buf[64] = {0};

	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%02m-%02dT%02H:%02M:%02SZ", &timeinfo);

	strcpy(startTimeString, strftime_buf);

	ESP_LOGW(TAG, "************** START TIME UTC: %s ****************", startTimeString);

	hasNewStartTime = true;
}

void chargeController_ClearNextStartTime()
{
	if(startTimeString[0] != '\0')
	{
		strcpy(startTimeString, "");
		hasNewStartTime = true;
		ESP_LOGI(TAG, "Cleared start time");
	}
	else
	{
		ESP_LOGW(TAG, "Start time already cleared");
	}
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

static bool sendScheduleDiagnostics = false;
void chargeController_SetSendScheduleDiagnosticsFlag()
{
	sendScheduleDiagnostics = true;
}

//#include "zones.h"
//#include "../components/micro_tz_db/zones.h"
/*#include "../components/utz/utz.h"
#include "../components/utz/zones.h"
*/
static uint16_t isPausedByAnySchedule = 0x0000;



bool chargecontroller_IsPauseBySchedule()
{
	if((isScheduleActive == true) && (isPausedByAnySchedule > 0))
		return true;
	else if((isScheduleActive == true) && (startDelayCounter > 0))
		return true;
	else
		return false;
}


static uint16_t previousIsPausedByAnySchedule = 0x0000;
static char scheduleString[150] = {0};
enum ChargerOperatingMode prevOpMode = CHARGE_OPERATION_STATE_UNINITIALIZED;
static bool applyDelayAtBoot = true;
static bool sentClearStartTimeAtBoot = false;

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

	/// When booting with car connected, the random delay should always be applied
	if((applyDelayAtBoot == true) && (opMode > CHARGE_OPERATION_STATE_DISCONNECTED))
	{
		startDelayCounter = randomStartDelay;
		applyDelayAtBoot = false;
	}

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
		/// Check if now-time is within pause interval

		struct tm updatedTimeStruct = {0};
		zntp_GetLocalTimeZoneStruct(&updatedTimeStruct, 3600 * 2);

		/// For testing - overrides time by command
		if(testWithNowTime)
		{
			updatedTimeStruct.tm_wday = nowTime.tm_wday;
			updatedTimeStruct.tm_hour = nowTime.tm_hour;
			updatedTimeStruct.tm_min = nowTime.tm_min;
			updatedTimeStruct.tm_sec = 00;
		}

		int minutesNow = updatedTimeStruct.tm_hour * 60 + updatedTimeStruct.tm_min;

		int tomorrow = updatedTimeStruct.tm_wday + 1;
		if (tomorrow == 7)
			tomorrow = 0;

		/// On the same day
		uint8_t shiftPositions = 0;
		if(updatedTimeStruct.tm_wday == 0)
			shiftPositions = 6;
		else
			shiftPositions = updatedTimeStruct.tm_wday;

		strcpy(scheduleString, "");

		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{

			///When ending override, clear all paused schedules to be able to re-pause and reset nextStopTime
			if((overrideTimer == 0) && (previousOverrideTimer > 0))
			{
				timeSchedules[scheduleNr].isPaused = false;
			}

			volatile bool isPauseDay = (timeSchedules[scheduleNr].Days & (0x01 << (shiftPositions - 1)));

			/// Check if schedule is applicable
			if((isPauseDay))
			{
				/// ARE WE INSIDE PAUSE INTERVAL?
				if((minutesNow >= timeSchedules[scheduleNr].StartTotalMinutes) && (minutesNow < timeSchedules[scheduleNr].StopTotalMinutes))
				{
					volatile bool isNextDayPauseDay = (timeSchedules[scheduleNr].Days & (0x01 << shiftPositions));

					if((timeSchedules[scheduleNr].isPaused == false))// || ((opMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (prevOpMode == CHARGE_OPERATION_STATE_DISCONNECTED)))
					{
						if(updatedTimeStruct.tm_wday == 6)
							chargeController_SetNextStartTime(0, scheduleNr, isNextDayPauseDay);
						else
							chargeController_SetNextStartTime(updatedTimeStruct.tm_wday, scheduleNr, isNextDayPauseDay);

						/// Make sure the randomStartDelay is reset for each schedule start
						startDelayCounter = randomStartDelay;
					}

					timeSchedules[scheduleNr].isPaused= true;

					int localTimeOffset = chargeController_GetLocalTimeOffset();
					if((timeSchedules[scheduleNr].SpanToNextDay == false) || (isNextDayPauseDay == false))
						snprintf(scheduleString, sizeof(scheduleString), "Within pause interval [%02i:%02i] -> %i|%02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%i (UTC+%i) (1)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin,    (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, randomStartDelay, localTimeOffset);
					else if(timeSchedules[scheduleNr].SpanToNextDay == true)
						snprintf(scheduleString, sizeof(scheduleString), "Within ext pause interval [%02i:%02i] -> %i|%02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%i (UTC+%i) (1)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, updatedTimeStruct.tm_wday, updatedTimeStruct.tm_hour, updatedTimeStruct.tm_min, timeSchedules[scheduleNr + 1].StopHour, timeSchedules[scheduleNr + 1].StopMin,    (int)(((timeSchedules[scheduleNr].StopTotalMinutes + timeSchedules[scheduleNr+1].StopTotalMinutes) - minutesNow)/60), ((timeSchedules[scheduleNr].StopTotalMinutes + timeSchedules[scheduleNr+1].StopTotalMinutes) - minutesNow) % 60, timeSchedules[scheduleNr+1].StopHour, timeSchedules[scheduleNr+1].StopMin, randomStartDelay, localTimeOffset);
					else
						snprintf(scheduleString, sizeof(scheduleString), "Undefined");
				}

				/// WE ARE IN THE ALLOW CHARGING STATE
				else
				{
					timeSchedules[scheduleNr].isPaused = false;
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
		}

		previousOverrideTimer = overrideTimer;

		/// If overriding by Cloud or BLE, replace the value of isPauseByAnySchedule
		if((isPausedByAnySchedule > 0) && (overrideTimer > 0))
				isPausedByAnySchedule = 0;
		else if((isPausedByAnySchedule == 0) && (overrideTimer > 0) && (opMode == CHARGE_OPERATION_STATE_DISCONNECTED))
		{
			overrideTimer = 0;
			ESP_LOGW(TAG, "***** Stopped OVERRIDE when car id disconnected ******");
		}
		else if((isPausedByAnySchedule == 0) && (startDelayCounter == 0) && (overrideTimer > 0))
		{
			overrideTimer = 0;
			ESP_LOGW(TAG, "***** Clear OVERRIDE between pausing ******");
		}


		if(isPausedByAnySchedule == 0)
		{
			/// ACTIVE

			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " ACTIVE (Pb: 0x%04X) Ov: %i", isPausedByAnySchedule, overrideTimer);

			if((opMode == CHARGE_OPERATION_STATE_DISCONNECTED) || (overrideTimer == 1))
			{
				startDelayCounter = 0;

				ESP_LOGW(TAG, "DISCONNECTED OUTSIDE SCHDULE -> REMOVE DELAY");

				if(overrideTimer == 1)
				{
					ESP_LOGW(TAG, "Starting due to OVERRIDE!");
					overrideTimer = 2;
				}
			}

			if((startDelayCounter > 0) && (opMode == CHARGE_OPERATION_STATE_PAUSED))// || (overrideTimer == 1))
			{
				startDelayCounter--;
			}

			if((opMode == CHARGE_OPERATION_STATE_PAUSED) && (GetFinalStopActiveStatus() == true) && (startDelayCounter == 0) && (GetFinalStopActiveStatus() == true))
			{
				ESP_LOGW(TAG, "***** Schedule ended -> sending resume command to MCU ******");
				chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_SCHEDULE);
			}
		}
		else
		{
			/// PAUSED

			startDelayCounter = randomStartDelay;

			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " PAUSED (Pb: 0x%04X) Ov: %i", isPausedByAnySchedule, overrideTimer);

			/// When schedule is paused, but MCU is active (e.g. ESP restart), then pause the MCU
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

		/// If booting outside pause, ensure any scheduled start time is cleared in Cloud
		if((sentClearStartTimeAtBoot == false) && (isPausedByAnySchedule == 0))
		{
			hasNewStartTime = true;
			sentClearStartTimeAtBoot = true;
		}

		ESP_LOGW(TAG, "IsPaused: %i, RandomDelayCounter %i/%i, Override: %i", isPausedByAnySchedule, startDelayCounter, randomStartDelay, overrideTimer);
		ESP_LOGW(TAG, "%i: %s", strlen(scheduleString), scheduleString);
	}

	if(sendScheduleDiagnostics == true)
	{
		publish_debug_telemetry_observation_Diagnostics(scheduleString);
		sendScheduleDiagnostics = false;
	}

	if((storage_Get_Standalone() == 1) && (opMode == CHARGE_OPERATION_STATE_REQUESTING))
	{
		chargeController_SendStartCommandToMCU();
	}

	prevOpMode = opMode;
	previousIsPausedByAnySchedule = isPausedByAnySchedule;
}


void chargeController_SetRandomStartDelay()
{
	/// Formula: int randomStartDelay = (esp_random() % (high - low + 1)) + low;
	if(strlen(scheduleString) >= 13)
	{
		randomStartDelay = 9;//(esp_random() % (maxStartDelay- 7 + 1) + 7);
		startDelayCounter = randomStartDelay;
	}
	else
	{
		randomStartDelay = 0;
		startDelayCounter = 0;
	}

	ESP_LOGE(TAG, "********* StartDelayCounter set to %i **************", startDelayCounter);
}


void chargeController_ClearRandomStartDelay()
{
	randomStartDelay = 0;
}


bool chargeController_SendStartCommandToMCU(enum ChargeSource source)
{
	ESP_LOGW(TAG, "Charging Requested by %i", source);
	bool retval = false;

	sessionHandler_ClearCarInterfaceResetConditions();

	enum ChargerOperatingMode chOpMode = MCU_GetChargeOperatingMode();
	if((chOpMode == CHARGE_OPERATION_STATE_REQUESTING) && (storage_Get_Standalone() == 1))
	{
		//Use Standalone current as if from cloud command
		float standAloneCurrent = storage_Get_StandaloneCurrent();
		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, standAloneCurrent);

		ESP_LOGW(TAG, "********* 1 Starting from state \"Standalone\" : CHARGE_OPERATION_STATE_REQUESTING ST-AC: %2.2fA **************", standAloneCurrent);

		if(ret == MsgWriteAck)
		{
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
		else
		{
			ESP_LOGW(TAG, "Sent start Command to MCU FAILED");
		}



	}
	else if(((chOpMode == CHARGE_OPERATION_STATE_REQUESTING) || (chOpMode == CHARGE_OPERATION_STATE_CHARGING)) && (storage_Get_Standalone() == 0) && (isPausedByAnySchedule == 0) && (startDelayCounter == 0))
	{
		ESP_LOGW(TAG, "********* 2 Starting from state CHARGE_OPERATION_STATE_REQUESTING **************");

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
	else if(chOpMode == CHARGE_OPERATION_STATE_PAUSED)
	{
		ESP_LOGW(TAG, "********* 3 Resuming from state CHARGE_OPERATION_STATE_PAUSED && FinalStop == true **************");

		MessageType ret = MCU_SendCommandId(CommandResumeChargingMCU);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "MCU CommandResumeChargingMCU command OK");
			SetFinalStopActiveStatus(0);
			chargeController_ClearNextStartTime();
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
		ESP_LOGE(TAG, "######## Failed to start: STATE: opmode: %i, std: %i, isPaused: %i, cnt %i ########", chOpMode, storage_Get_Standalone(), isPausedByAnySchedule, startDelayCounter);
	}

	return retval;
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
