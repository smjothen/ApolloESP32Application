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

}


void chargeSession_End()
{
	GetUTCTimeString(chargeSession.EndTime);

	ESP_LOGI(TAG, "End time is: %s", chargeSession.EndTime);
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
