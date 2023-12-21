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
#include "zaptec_cloud_observations.h"
#include "zaptec_cloud_listener.h"
#include "utz.h"
#include "zones.h"
#include "chargeSession.h"

#include <esp_random.h>

static const char *TAG = "CHARGECONTROL  ";


//const uint32_t maxStartDelay = 60;//15;//600;

static uint32_t startDelayCounter = 0;
static uint32_t randomStartDelay = 0;
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
static float previousStandaloneCurrent = 0.0;

static utz_t ctx = {0};

void chargeController_Init()
{
	ESP_LOGI(TAG, "SETTING TIMER");
	//Create timer to control chargetime countdown
	TickType_t startChargeTimer = pdMS_TO_TICKS(1000); //1 second
	TimerHandle_t startTimerHandle = xTimerCreate( "StartChargeTimer", startChargeTimer, pdTRUE, NULL, RunStartChargeTimer);
	xTimerReset( startTimerHandle, portMAX_DELAY);

	previousStandaloneCurrent = storage_Get_StandaloneCurrent();
	chargeController_Activation();
	if(enforceScheduleAndDelay == true)
		chargeController_SetRandomStartDelay();
}

void chargeController_Activation()
{
	//DEBUG
	//storage_Set_Location("NOR");//"GBR"//
	//storage_Set_Timezone("Europe/Oslo");
	//storage_Set_TimeSchedule(fullTimeScheduleString);

	if((strncmp(storage_Get_Location(), "GBR", 3) == 0) && (strlen(storage_Get_TimeSchedule()) >= 13))
	{
		ESP_LOGI(TAG, "ENFORCING SCHEDULE AND DELAY");
		enforceScheduleAndDelay = true;
		chargeController_SetStandaloneState(storage_Get_session_controller());
		/// Don't need to call storage_SaveConfiguration() here because only MCU will potentially be changed
		chargeController_SetTimes();
		isScheduleActive = true;
	}
	else
	{
		enforceScheduleAndDelay = false;
		chargeController_SetStandaloneState(storage_Get_session_controller());
		isScheduleActive = false;
	}

	ESP_LOGW(TAG, "Schedule: %s, %s, %s, SCHEDULE %s", storage_Get_Location(), storage_Get_Timezone(), storage_Get_TimeSchedule(), isScheduleActive ? "ON" : "OFF");
}

static udatetime_t nowTime = {0};
static ulocaltime_t nowTimeLocal = {0};
static bool testWithNowTime = false;

int chargeController_GetZone(uzone_t *zone) {
	char *zoneId = storage_Get_Timezone();

	if (utz_zone_by_id(&ctx, zoneId, zone)) {
		ESP_LOGW(TAG, "UTZ           : Unknown zone %s, defaulting to Etc/UTC", zoneId);
		if (utz_zone_by_id(&ctx, "Etc/UTC", zone)) {
			return -1;
		}
	}

	return 0;
}

void chargeController_SetNowTime(char * timeString)
{
	char buf[64];

	if(utz_datetime_parse_iso(timeString, &nowTime) == 0)
	{
		testWithNowTime = true;

		uzone_t zone;
		chargeController_GetZone(&zone);

		nowTimeLocal.datetime = nowTime;
		utz_local_resolve(&ctx, &nowTimeLocal, &zone);

		utz_datetime_format_iso(buf, sizeof (buf), &nowTimeLocal.datetime);
		ESP_LOGI(TAG, "UTZ    NOWTIME: Set %s", buf);
		utz_datetime_format_iso(buf, sizeof (buf), &nowTimeLocal.utctime);
		ESP_LOGI(TAG, "UTZ    NOWTIME: Set %s UTC", buf);
	}
	else
	{
		testWithNowTime = false;
		ESP_LOGI(TAG, "UTZ    NOWTIME: Cleared");
	}
}

int chargeController_GetWeekDay(ulocaltime_t *localTime) {
	if (testWithNowTime) {
		return utz_datetime_day_of_week(&nowTimeLocal.datetime);
	}

	return utz_datetime_day_of_week(&localTime->datetime);
}

/*
 * Calculates the local time for an IANA zone at a given UTC time.
 * Returns the local offset (in seconds) from UTC.
 */
int chargeController_GetLocalTime(udatetime_t *utcTime, ulocaltime_t *localTime)
{
	if (testWithNowTime) {
		*localTime = nowTimeLocal;
		return nowTimeLocal.offset;
	}

	uzone_t zone;
	chargeController_GetZone(&zone);

	utz_utc_to_local(&ctx, utcTime, localTime, &zone);

	//ESP_LOGW(TAG, "TZ: %s, offset: %d", zone.name, localTime->offset);

	return localTime->offset;
}

/*
 * Calculates the current local time for an IANA zone.
 * Returns the local offset (in seconds) from UTC.
 */
int chargeController_GetLocalTimeNow(ulocaltime_t *localTime) {
	udatetime_t utcNow;
	utz_datetime_init_utc(&utcNow);
	return chargeController_GetLocalTime(&utcNow, localTime);
}

/*
 * Returns the local offset (in seconds) from UTC.
 */
int chargeController_GetLocalTimeOffset(udatetime_t *utcTime)
{
	ulocaltime_t localTime;
	return chargeController_GetLocalTime(utcTime, &localTime);
}

void chargeController_WriteNewTimeSchedule(char * timeSchedule)
{
	storage_Set_TimeSchedule(timeSchedule);

	ESP_LOGW(TAG, "Set timeSchedule from Cloud: %s", storage_Get_TimeSchedule());
}

static uint16_t isPausedByAnySchedule = 0x0000;

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

	memset(timeSchedules, 0, sizeof(timeSchedules));
	isPausedByAnySchedule = 0;

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
			timeSchedules[i].SpanToNextDay = false;

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

static char startTimeString[32] = {0};
static bool hasNewStartTime = false;

/*
 * Calculates the next charge start time in UTC from the stop time of the schedule in local time.
 */
static void chargeController_SetNextStartTime(ulocaltime_t *localTimeNow, int currentSchedule, bool isNextDayPauseDay)
{
	char strftime_buf[64] = {0};

	uzone_t zone;
	chargeController_GetZone(&zone);

	int stopSchedule = currentSchedule;

	if(timeSchedules[currentSchedule].SpanToNextDay && isNextDayPauseDay)
		stopSchedule = currentSchedule + 1;

	// Copy so we don't overwrite original value
	udatetime_t stopLocal = localTimeNow->datetime;
	// Local date with schedule stop time
	utz_set_hms(&stopLocal, timeSchedules[stopSchedule].StopHour, timeSchedules[stopSchedule].StopMin, 0);

	if (utz_datetime_compare(&localTimeNow->datetime, &stopLocal) >= 0) {
		ESP_LOGI(TAG, "***** 1. Tomorrow *****");
		utz_datetime_add(&stopLocal, &stopLocal, 86400 + randomStartDelay);
	} else {
		ESP_LOGI(TAG, "***** 2. Today *****");
		utz_datetime_add(&stopLocal, &stopLocal, randomStartDelay);
	}

	// Now convert local to UTC to take into account DST/TZ changes
	udatetime_t stopUTC;
	int offset = utz_local_to_utc(&ctx, &stopLocal, &stopUTC, &zone);

	int offHours = offset / 3600;
	int offMins = (abs(offset) % 3600) / 60;

	ESP_LOGI(TAG, "UTZ       ZONE: %s", zone.name);
	utz_datetime_format_iso(strftime_buf, sizeof (strftime_buf), &stopLocal);
	ESP_LOGI(TAG, "UTZ STOP LOCAL: %s (UTC%+d:%02d)", strftime_buf, offHours, offMins);
	utz_datetime_format_iso_utc(strftime_buf, sizeof (strftime_buf), &stopUTC);
	ESP_LOGI(TAG, "UTZ   STOP UTC: %s", strftime_buf);

	utz_datetime_format_iso_utc(strftime_buf, sizeof (strftime_buf), &stopUTC);
	strcpy(startTimeString, strftime_buf);

	ESP_LOGI(TAG, "************** START TIME UTC: %s ****************", startTimeString);

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
		ESP_LOGI(TAG, "Start time already cleared");
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
static char scheduleString[160] = {0};
enum ChargerOperatingMode prevOpMode = CHARGE_OPERATION_STATE_UNINITIALIZED;
static bool applyDelayAtBoot = true;
static bool sentClearStartTimeAtBoot = false;

static bool pausedByCloudCommand = false;
void chargeController_SetPauseByCloudCommand(bool pausedState)
{
	if(isPausedByAnySchedule == 0)
		pausedByCloudCommand = pausedState;
}

static uint8_t printCtrl = 3;

void RunStartChargeTimer()
{
	enum ChargerOperatingMode opMode = MCU_GetChargeOperatingMode();
	enum CarChargeMode chargeMode = MCU_GetChargeMode();

	/// When booting with car connected, the random delay should always be applied
	if((applyDelayAtBoot == true) && (chargeMode != eCAR_UNINITIALIZED))
	{
		if(chargeMode < eCAR_DISCONNECTED)
		{
			startDelayCounter = randomStartDelay;
		}

		applyDelayAtBoot = false;
	}

	/// This check is needed to ensure NextStartTime is communicated and cleared correctly
	if(hasBeenDisconnected == true)
	{
		ESP_LOGI(TAG, "**** DISCONNECTED: Clearing all paused-flags ****");
		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{
			if(timeSchedules[scheduleNr].isPaused == true)
			{
				//ESP_LOGE(TAG, "SETTING TO FALSE");
				timeSchedules[scheduleNr].isPaused = false;
			}
		}

		hasBeenDisconnected = false;
		chargeController_ClearNextStartTime();

		pausedByCloudCommand = false;
	}


	if(isScheduleActive == true)
	{
		/// Check if now-time is within pause interval

		ulocaltime_t localTimeNow;
		chargeController_GetLocalTimeNow(&localTimeNow);

		udatetime_t *localDtNow = &localTimeNow.datetime;

		// 1 = Monday, ..., 7 = Sunday
		int weekDay = chargeController_GetWeekDay(&localTimeNow);
		int weekDayTom = weekDay == 7 ? 1 : weekDay + 1;

		int dayMask = 1 << (weekDay - 1);
		int dayMaskTom = 1 << (weekDayTom - 1);

		int localHour = utz_hour(localDtNow);
		int localMin = utz_minute(localDtNow);

		int minutesNow = localHour * 60 + localMin;

		strcpy(scheduleString, "");

		int scheduleNr;
		for(scheduleNr = 0; scheduleNr < nrOfSchedules; scheduleNr++)
		{
			///When ending override, clear all paused schedules to be able to re-pause and reset nextStopTime
			if((overrideTimer == 0) && (previousOverrideTimer > 0))
			{
				timeSchedules[scheduleNr].isPaused = false;
			}

			volatile bool isPauseDay = timeSchedules[scheduleNr].Days & dayMask;

			/// Check if schedule is applicable
			if((isPauseDay))
			{
				/// ARE WE INSIDE PAUSE INTERVAL?
				if((minutesNow >= timeSchedules[scheduleNr].StartTotalMinutes) && (minutesNow < timeSchedules[scheduleNr].StopTotalMinutes))
				{
					volatile bool isNextDayPauseDay = timeSchedules[scheduleNr].Days & dayMaskTom;

					if(timeSchedules[scheduleNr].isPaused == false)// || ((opMode > CHARGE_OPERATION_STATE_DISCONNECTED) && (prevOpMode == CHARGE_OPERATION_STATE_DISCONNECTED)))
					{
						chargeController_SetNextStartTime(&localTimeNow, scheduleNr, isNextDayPauseDay);

						/// Make sure the randomStartDelay is reset for each schedule start
						startDelayCounter = randomStartDelay;
					}

					timeSchedules[scheduleNr].isPaused= true;

					int localTimeOffset = localTimeNow.offset;
					int localTimeOffsetHours = localTimeOffset / 3600;
					int localTimeOffsetMins = (abs(localTimeOffset) % 3600) / 60;

					if((timeSchedules[scheduleNr].SpanToNextDay == false) || (isNextDayPauseDay == false))
						snprintf(scheduleString, sizeof(scheduleString), "Within pause interval [%02i:%02i] -> %i|%02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%" PRIi32 " (UTC%+d:%02d) (1)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, weekDay, localHour, localMin, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin,    (int)((timeSchedules[scheduleNr].StopTotalMinutes - minutesNow)/60), (timeSchedules[scheduleNr].StopTotalMinutes - minutesNow) % 60, timeSchedules[scheduleNr].StopHour, timeSchedules[scheduleNr].StopMin, randomStartDelay, localTimeOffsetHours, localTimeOffsetMins);
					else if(timeSchedules[scheduleNr].SpanToNextDay == true)
						snprintf(scheduleString, sizeof(scheduleString), "Within ext pause interval [%02i:%02i] -> %i|%02i:%02i -> [%02i:%02i]. Resuming in %02i hours %02i min at %02i:%02i +%" PRIi32 " (UTC%+d:%02d) (1)", timeSchedules[scheduleNr].StartHour, timeSchedules[scheduleNr].StartMin, weekDay, localHour, localMin, timeSchedules[scheduleNr + 1].StopHour, timeSchedules[scheduleNr + 1].StopMin,    (int)(((timeSchedules[scheduleNr].StopTotalMinutes + timeSchedules[scheduleNr+1].StopTotalMinutes) - minutesNow)/60), ((timeSchedules[scheduleNr].StopTotalMinutes + timeSchedules[scheduleNr+1].StopTotalMinutes) - minutesNow) % 60, timeSchedules[scheduleNr+1].StopHour, timeSchedules[scheduleNr+1].StopMin, randomStartDelay, localTimeOffsetHours, localTimeOffsetMins);
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
					ESP_LOGW(TAG, "No schedule for today - %i - Allowing charging", weekDay);
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

			/// Clear next event in two cases:
			/// 1) Schedule ended with no car connected,
			/// 2) Schedule ended with car connected and starDelayCounter deactivated.
			if(((previousIsPausedByAnySchedule > 0) && (startDelayCounter == 0)) || (overrideTimer == 1))
				chargeController_ClearNextStartTime();

			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " ACTIVE (Pb: 0x%04X) RDC:%" PRIi32 "/%" PRIi32 " Ov:%i", isPausedByAnySchedule, startDelayCounter, randomStartDelay, overrideTimer);

			if(overrideTimer == 1)
			{
				startDelayCounter = 0;

				//ESP_LOGW(TAG, "DISCONNECTED OUTSIDE SCHDULE -> REMOVE DELAY");

				if(overrideTimer == 1)
				{
					ESP_LOGW(TAG, "Starting due to OVERRIDE!");
					overrideTimer = 2;
				}
			}


			if((startDelayCounter > 0) && ((opMode == CHARGE_OPERATION_STATE_PAUSED) || (opMode == CHARGE_OPERATION_STATE_REQUESTING)))// || (overrideTimer == 1))
			{
				startDelayCounter--;
				if((startDelayCounter == 0) && (storage_Get_Standalone() == 0) && isMqttConnected())
				{
					ESP_LOGW(TAG, "Sending requesting (sched)");
					publish_debug_telemetry_observation_ChargingStateParameters();
				}
			}

			if((opMode == CHARGE_OPERATION_STATE_PAUSED) && (GetFinalStopActiveStatus() == true) && (startDelayCounter == 0))
			{
				if(pausedByCloudCommand == false)
				{
					ESP_LOGW(TAG, "***** Schedule ended -> sending resume command to MCU ******");
					chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_SCHEDULE);
				}
			}
		}
		else
		{
			/// PAUSED

			startDelayCounter = randomStartDelay;

			snprintf(scheduleString+strlen(scheduleString), sizeof(scheduleString), " PAUSED (Pb: 0x%04X) RDC:%" PRIi32 "/%" PRIi32 " Ov:%i", isPausedByAnySchedule, startDelayCounter, randomStartDelay, overrideTimer);

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
		}
		sentClearStartTimeAtBoot = true;

		printCtrl--;
		if(printCtrl == 0)
		{
			ESP_LOGW(TAG, "%i: %s", strlen(scheduleString), scheduleString);
			printCtrl = 3;
		}
	}
	else
	{
		//Here schedule is not active, but start delay applies. Send requesting state to Cloud when done.
		//ESP_LOGW(TAG, "startDelayCounter: %i", startDelayCounter);
		isPausedByAnySchedule = 0x0000;
		if(startDelayCounter > 0)
		{
			startDelayCounter--;
			//ESP_LOGW(TAG, "startDelayCounter: %i", startDelayCounter);
			if((startDelayCounter == 0) && (storage_Get_Standalone() == 0) && isMqttConnected())
			{
				ESP_LOGW(TAG, "Sending requesting (no sched)");
				publish_debug_telemetry_observation_ChargingStateParameters();
			}
		}
	}

	if(sendScheduleDiagnostics == true)
	{
		publish_debug_telemetry_observation_Diagnostics(scheduleString);
		sendScheduleDiagnostics = false;
	}

	if((storage_Get_Standalone() == 1) && (isScheduleActive == true))
	{
		if(opMode == CHARGE_OPERATION_STATE_REQUESTING)
		{
			if(startDelayCounter == 0)
			{
				chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_STANDALONE);
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
		float standaloneCurrent = storage_Get_StandaloneCurrent();
		if (standaloneCurrent != previousStandaloneCurrent)
		{
			float currentToSend = standaloneCurrent;
			if(MCU_GetGridType() == NETWORK_3P3W)
				currentToSend = standaloneCurrent/1.732;

			MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, currentToSend);
			if(ret == MsgWriteAck)
			{
				ESP_LOGW(TAG, "Updating Standalone current %2.2f -> %2.2fA(IT3: %2.2fA)", previousStandaloneCurrent, standaloneCurrent, currentToSend);
				previousStandaloneCurrent = standaloneCurrent;
			}
			else
			{
				ESP_LOGE(TAG, "Failed STAC");
			}
		}
	}

	if(chargeMode == eCAR_DISCONNECTED)
	{
		//ESP_LOGW(TAG, "Clearing overrideTimer");
		overrideTimer = 0;
		startDelayCounter = 0;
	}

	prevOpMode = opMode;
	previousIsPausedByAnySchedule = isPausedByAnySchedule;
}


void chargeController_SetRandomStartDelay()
{
	/// Formula: int randomStartDelay = (esp_random() % (high - low + 1)) + low;
	uint32_t high = storage_Get_MaxStartDelay();

	if(high > 0)
	{
		uint32_t tmp = (esp_random() % (high - 1 + 1) + 1);

		//Sanity check to avoid rollover to high values if high < low during testing
		if(tmp > 3600)
			randomStartDelay = 600;
		else
			randomStartDelay = tmp;
	}
	else
	{
		chargeController_ClearRandomStartDelay();
	}

	ESP_LOGW(TAG, "randomStartDelay set to %" PRIi32 "", randomStartDelay);
}


void chargeController_ClearRandomStartDelay()
{
	randomStartDelay = 0;
	startDelayCounter = 0;
}


bool chargeController_SendStartCommandToMCU(enum ChargeSource source)
{
	ESP_LOGW(TAG, "Charging Requested by %i", source);
	bool retval = false;

	sessionHandler_ClearCarInterfaceResetConditions();

	enum ChargerOperatingMode chOpMode = MCU_GetChargeOperatingMode();
	if((chOpMode == CHARGE_OPERATION_STATE_REQUESTING) && (storage_Get_Standalone() == 1) && (isScheduleActive == true))
	{
		//Do not Continue if not authenticated
		if((storage_Get_AuthenticationRequired() == true) && (chargeSession_IsAuthenticated() == false))
			return retval;

		//Use Standalone current as if from cloud command
		float standAloneCurrent = storage_Get_StandaloneCurrent();

		float currentToSend = standAloneCurrent;
		if(MCU_GetGridType() == NETWORK_3P3W)
			currentToSend = standAloneCurrent/1.732;

		MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, currentToSend);

		ESP_LOGW(TAG, "********* 1 Starting from state \"Standalone\" : CHARGE_OPERATION_STATE_REQUESTING ST-AC: %2.2fA(IT3: %2.2fA) **************", standAloneCurrent, currentToSend);

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
		ESP_LOGE(TAG, "######## Failed to start: STATE: opmode: %i, std: %i, isPaused: %i, cnt %" PRIi32 " ########", chOpMode, storage_Get_Standalone(), isPausedByAnySchedule, startDelayCounter);
	}

	return retval;
}



/*
 * Ensure storage_SaveConfiguration() is called when this function returns true
 */
bool chargeController_SetStandaloneState(enum session_controller controller)
{
	enum session_controller wanted_controller;
	if(controller == eSESSION_OCPP)
	{
		if(controller & eCONTROLLER_MCU_STANDALONE && enforceScheduleAndDelay){
			wanted_controller = eSESSION_ZAPTEC_CLOUD;
		}else{
			wanted_controller = controller;
		}
	}
	else
	{
		///When non-ocpp sessionController is synced, use the standalone value to determine states, since 
		/// sessionController value in device is not updated with latest user-configured standalone mode.
		if((storage_Get_Standalone() == 1) && enforceScheduleAndDelay){
			wanted_controller = eSESSION_ZAPTEC_CLOUD;
		}else if(storage_Get_Standalone() == 0){
			wanted_controller = eSESSION_ZAPTEC_CLOUD;
		}else{
			wanted_controller = eSESSION_STANDALONE;
		}
	}

	MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, (wanted_controller & eCONTROLLER_MCU_STANDALONE) ? 1 : 0); 	//MCU must be controlled by ESP due to schedule function
	if(ret == MsgWriteAck)
	{
		storage_Set_session_controller(controller);
		ESP_LOGI(TAG, "Set Standalone: MCU=%s ESP=%s\n",
				(wanted_controller & eCONTROLLER_MCU_STANDALONE) ? "True" : "False",
				(controller & eCONTROLLER_ESP_STANDALONE) ? "True" : "False");
		return true;
	}
	else
	{
		ESP_LOGE(TAG, "MCU standalone parameter error");
		return false;
	}
}
