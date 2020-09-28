#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "chargeSession.h"
//#include <time.h>
#include <sys/time.h>
#include "sntp.h"
#include <string.h>
#include "connectivity.h"
#include "cJSON.h"
#include "storage.h"



static const char *TAG = "CHARGESESSION:     ";

static struct ChargeSession chargeSession = {0};


static void ChargeSession_Set_GUID()
{
	volatile uint32_t GUID[4] = {0};
	for (int i = 0; i < 4; i++)
		GUID[i] = esp_random();

//	ESP_LOGW(TAG, "GUID: %08x", GUID[3]);
//	ESP_LOGW(TAG, "GUID: %08x", GUID[2]);
//	ESP_LOGW(TAG, "GUID: %08x", GUID[1]);
//	ESP_LOGW(TAG, "GUID: %08x", GUID[0]);
	
	sprintf(chargeSession.SessionId, "%08x-%04x-%04x-%04x-%04x%08x", GUID[3], (GUID[2] >> 16), (GUID[2] & 0xFFFF), (GUID[1] >> 16), (GUID[1] & 0xFFFF), GUID[0]);
	ESP_LOGW(TAG, "GUID: %s", chargeSession.SessionId);

}

void GetUTCTimeString(char * timeString)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	char strftime_buf[64] = {0};

	time(&now);
	localtime_r(&now, &timeinfo);

	struct timeval t_now;
	gettimeofday(&t_now, NULL);

	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%02m-%02dT%02H:%02M:%02S", &timeinfo);

	sprintf(strftime_buf+strlen(strftime_buf), ".%06dZ", (uint32_t)t_now.tv_usec);
	strcpy(timeString, strftime_buf);
}


static void ChargeSession_Set_StartTime()
{
	GetUTCTimeString(chargeSession.StartTime);

	ESP_LOGI(TAG, "Start time is: %s", chargeSession.StartTime);
}


void chargeSession_Start()
{
	/// First check for resetSession on Flash
	esp_err_t readErr = chargeSession_ReadSessionResetInfo();

	if (readErr != ESP_OK)
	{
		if(readErr == 4354)
			ESP_LOGE(TAG, "chargeSession_ReadSessionResetInfo(): No file found: %d. Cleaning session and returning", readErr);
		else
			ESP_LOGE(TAG, "chargeSession_ReadSessionResetInfo() failed: %d. Cleaning session and returning", readErr);

		memset(&chargeSession, 0, sizeof(chargeSession));
	}

	if((strlen(chargeSession.SessionId) == 36) && (readErr == ESP_OK))
	{
		ESP_LOGI(TAG, "chargeSession_Start() using resetSession");
	}
	else
	{
		/// If no resetSession is found on Flash create new session
		memset(&chargeSession, 0, sizeof(chargeSession));
		ChargeSession_Set_GUID();
		if(connectivity_GetSNTPInitialized() == true)
		{
			ChargeSession_Set_StartTime();
			chargeSession.ReliableClock = true;
		}
		else
		{
			chargeSession.ReliableClock = false;
		}

		esp_err_t saveErr = chargeSession_SaveSessionResetInfo();
		if (saveErr != ESP_OK)
		{
			ESP_LOGE(TAG, "chargeSession_SaveSessionResetInfo() failed: %d", saveErr);
		}
	}
}


void chargeSession_End()
{
	GetUTCTimeString(chargeSession.EndTime);

	ESP_LOGI(TAG, "End time is: %s", chargeSession.EndTime);

	esp_err_t clearErr = storage_clearSessionResetInfo();
	ESP_LOGI(TAG, "Clearing csResetSession file");

	if (clearErr != ESP_OK)
	{
		ESP_LOGE(TAG, "storage_clearSessionResetInfo() failed: %d", clearErr);
	}
}



void chargeSession_SetAuthenticationCode(char * idAsString)
{
	strcpy(chargeSession.AuthenticationCode, idAsString);
}


void chargeSession_SetEnergy(float energy)
{
	chargeSession.Energy = energy;
}


struct ChargeSession chargeSession_Get()
{
	return chargeSession;
}


int chargeSession_GetSessionAsString(char * message)
{
	cJSON *CompletedSessionObject = cJSON_CreateObject();
	if(CompletedSessionObject == NULL){return -10;}

	cJSON_AddStringToObject(CompletedSessionObject, "SessionId", chargeSession.SessionId);
	cJSON_AddNumberToObject(CompletedSessionObject, "Energy", (float)chargeSession.Energy);
	cJSON_AddStringToObject(CompletedSessionObject, "StartDateTime", chargeSession.StartTime);
	cJSON_AddStringToObject(CompletedSessionObject, "EndDateTime", chargeSession.EndTime);
	cJSON_AddBoolToObject(CompletedSessionObject, "ReliableClock", chargeSession.ReliableClock);
	cJSON_AddBoolToObject(CompletedSessionObject, "StoppedByRFID", chargeSession.StoppedByRFID);
	cJSON_AddStringToObject(CompletedSessionObject, "AuthenticationCode", chargeSession.AuthenticationCode);

	char *buf = cJSON_PrintUnformatted(CompletedSessionObject);

	strcpy(message, buf);

	ESP_LOGI(TAG, "Made CompletedSessionObject");

	cJSON_Delete(CompletedSessionObject);
	free(buf);

	return 0;
}


esp_err_t chargeSession_SaveSessionResetInfo()
{
	ESP_LOGI(TAG, "Saving resetSession: %s, Start: %s - %d,  %f W, %s", chargeSession.SessionId, chargeSession.StartTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
	esp_err_t err = storage_SaveSessionResetInfo(chargeSession.SessionId, chargeSession.StartTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "chargeSession_SaveSessionResetInfo() failed: %d", err);

	return err;
}

esp_err_t chargeSession_ReadSessionResetInfo()
{
	esp_err_t err = ESP_OK;

	if(strlen(chargeSession.SessionId) != 36)
	{
		ESP_LOGI(TAG, "No SessionId, checking flash for resetSession");

		err = storage_ReadSessionResetInfo(chargeSession.SessionId, chargeSession.StartTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
		if (err != ESP_OK)
		{
			return err;
		}

		if(strlen(chargeSession.SessionId) == 36)
		{
			ESP_LOGI(TAG, "Loaded resetSession: %s, Start: %s - %d,  %f W, %s", chargeSession.SessionId, chargeSession.StartTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
		}
		else
		{
			ESP_LOGI(TAG, "No SessionId, found on flash. Starting with clean session");
		}
	}
	return err;
}