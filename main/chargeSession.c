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
#include "protocol_task.h"
#include "OCMF.h"
#include "offlineSession.h"
#include <math.h>



static const char *TAG = "CHARGESESSION  ";

static struct ChargeSession chargeSession = {0};

static bool hasNewSessionIdFromCloud = false;

static char * basicOCMF = "OCMF|{}";

static char sidOrigin[6] = {0};

static bool isCarConnected = false;

static bool hasReceivedStartChargingCommand = false;

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
	//hasNewSessionIdFromCloud = true;
	strcpy(sidOrigin, "local");
	ESP_LOGI(TAG, "GUID: %s (%s)", chargeSession.SessionId, sidOrigin);

	hasNewSessionIdFromCloud = false;
}

void chargeSession_PrintSession(bool online, bool pingReplyActive)
{
	//ESP_LOGW(TAG," %s - %s", storage_Get_Standalone() ? "STANDALONE": "SYSTEM", storage_Get_AuthenticationRequired() ? "AUTH" : "NO-AUTH");
	//ESP_LOGI(TAG," %s - %s\n SessionId: \t\t%s (%s)\n Energy: \t\t%f\n StartDateTime: \t%s\n EndDateTime: \t\t%s\n ReliableClock: \t%i\n StoppedByRFIDUid: \t%i\n AuthenticationCode: \t%s", storage_Get_Standalone() ? "STANDALONE": "SYSTEM", storage_Get_AuthenticationRequired() ? "AUTH" : "NO-AUTH", chargeSession.SessionId, sidOrigin, chargeSession.Energy, chargeSession.StartTime, chargeSession.EndTime, chargeSession.ReliableClock, chargeSession.StoppedByRFID, chargeSession.AuthenticationCode);

	//ESP_LOGI(TAG,"");
	printf("\r\n");
	ESP_LOGI(TAG,"%s - %s - %s - PINGR:%d\n \t\t\t\t  SessionId: \t\t%s (%s)\n \t\t\t\t  Energy: \t\t%f\n \t\t\t\t  StartDateTime: \t%s\n \t\t\t\t  EndDateTime: \t\t%s\n \t\t\t\t  ReliableClock: \t%i\n \t\t\t\t  StoppedByRFIDUid: \t%i\n \t\t\t\t  AuthenticationCode: \t%s", storage_Get_Standalone() ? "STANDALONE": "SYSTEM", storage_Get_AuthenticationRequired() ? "AUTH" : "NO-AUTH", online ? "ONLINE":"OFFLINE", pingReplyActive, chargeSession.SessionId, sidOrigin, chargeSession.Energy, chargeSession.StartDateTime, chargeSession.EndDateTime, chargeSession.ReliableClock, chargeSession.StoppedByRFID, chargeSession.AuthenticationCode);
	printf("\r\n");
	//ESP_LOGI(TAG,"");
}

void SetCarConnectedState(bool connectedState)
{
	isCarConnected = connectedState;
}

bool chargeSession_IsCarConnected()
{
	return isCarConnected;
}


char * chargeSession_GetSessionId()
{
	return chargeSession.SessionId;
}

bool chargeSession_HasNewSessionId()
{
	return hasNewSessionIdFromCloud;
}

void chargeSession_ClearHasNewSession()
{
	hasNewSessionIdFromCloud = false;
}


/*
 * This function returns the format required for CompletedSession
 */
void GetUTCTimeString(char * timeString, time_t *epochSec, uint32_t *epochUsec)
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
	*epochSec = now;
	*epochUsec = (uint32_t)t_now.tv_usec;
}


static void ChargeSession_Set_StartTime()
{
	time_t time_out;
	GetUTCTimeString(chargeSession.StartDateTime, &chargeSession.EpochStartTimeSec, &chargeSession.EpochStartTimeUsec);

	ESP_LOGI(TAG, "Start time is: %s (%d.%d)", chargeSession.StartDateTime, (uint32_t)chargeSession.EpochStartTimeSec, chargeSession.EpochStartTimeUsec);
}

int8_t chargeSession_SetSessionIdFromCloud(char * sessionIdFromCloud)
{
	if(isCarConnected == false)
	{
		ESP_LOGE(TAG, "#### Tried setting Cloud SessionId with car disconnected: %d ####", isCarConnected);
		return -1;
	}
//	else
//	{
//		ESP_LOGW(TAG, "**** Car connected: %d ***", isCarConnected);
//	}

	if(strcmp(sessionIdFromCloud, chargeSession.SessionId) == 0)
	{
		ESP_LOGI(TAG, "SessionId already set");

		//Only resend sessionId to cloud if we have never received a start command - like when waiting in eco-mode
		if(hasReceivedStartChargingCommand == false)
		{
			hasNewSessionIdFromCloud = true;
		}

		return 1;
	}

	if(strlen(chargeSession.SessionId) > 0)
	{
		ESP_LOGE(TAG, "SessionId was already set: %s. Overwriting.", chargeSession.SessionId);
	}

	strcpy(chargeSession.SessionId, sessionIdFromCloud);
	hasNewSessionIdFromCloud = true;

	strcpy(sidOrigin, "cloud");

	ESP_LOGI(TAG, "SessionId: %s (%s), len: %d\n", chargeSession.SessionId, sidOrigin, strlen(chargeSession.SessionId));

	if(chargeSession.StartDateTime[0] == '\0')
	{
		ESP_LOGI(TAG, "Setting cloud start time");
		ChargeSession_Set_StartTime();
	}

	//Save
	chargeSession_SaveSessionResetInfo();

	return 0;
}



void chargeSession_Start()
{
	/// First check for resetSession on Flash
	esp_err_t readErr = chargeSession_ReadSessionResetInfo();

	//indicate that the energy value is invalid
	//chargeSession.Energy = -1.0;

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
		strcpy(sidOrigin, "file ");

		chargeSession.SignedSession = basicOCMF;
		//Read from last file.
		//OCMF_CreateNewOCMFLog(chargeSession.EpochStartTimeSec);
	}
	else
	{
		/// If no resetSession is found on Flash create new session
		memset(&chargeSession, 0, sizeof(chargeSession));
		ChargeSession_Set_GUID();
		ChargeSession_Set_StartTime();


		if(connectivity_GetSNTPInitialized() == true)
		{
			chargeSession.ReliableClock = true;
		}
		else
		{
			chargeSession.ReliableClock = false;
			ESP_LOGE(TAG, "NO SESSION START TIME SET!");
		}

		chargeSession.SignedSession = basicOCMF;
		//OCMF_CreateNewOCMFLog(chargeSession.EpochStartTimeSec);
		//OCMF_NewOfflineSessionEntry();

		char * sessionData = calloc(1000,1);
		chargeSession_GetSessionAsString(sessionData);
		esp_err_t saveErr = offlineSession_SaveSession(sessionData);
		free(sessionData);

		if (saveErr != ESP_OK)
		{
			ESP_LOGE(TAG, "offlineSession_SaveSession() failed: %d", saveErr);
		}

		OCMF_CompletedSession_StartStopOCMFLog('B', chargeSession.EpochStartTimeSec);

	}

	//Add for new and flash-read sessions
	//strcpy(chargeSession.SignedSession,"OCMF|{}"); //TODO: Increase string length if changing content
	//chargeSession.SignedSession[7]='\0';

	///chargeSession.SignedSession = basicOCMF;
	///OCMF_CreateNewOCMFLog();
}



void chargeSession_UpdateEnergy()
{
	if(strlen(chargeSession.SessionId) > 0)
	{
		float energy = MCU_GetEnergy();

		if(energy > 0.001)
		{
			energy = roundf(energy * 1000) / 1000; /// round to 3rd desimal to remove unnecessary desimals

			//Only allow significant, positive, increasing energy
			if(energy > chargeSession.Energy)
			{
				chargeSession.Energy = energy;
			}
		}
	}
	else
	{
		chargeSession.Energy = 0.0;
	}

}

void chargeSession_Finalize()
{
	//chargeSession.Energy = MCU_GetEnergy();
	chargeSession_UpdateEnergy();
	GetUTCTimeString(chargeSession.EndDateTime, &chargeSession.EpochEndTimeSec, &chargeSession.EpochEndTimeUsec);

	ESP_LOGI(TAG, "End time is: %s (%d.%d)", chargeSession.EndDateTime, (uint32_t)chargeSession.EpochEndTimeSec, chargeSession.EpochEndTimeUsec);


	/// Create the 'E' message
	OCMF_CompletedSession_StartStopOCMFLog('E', chargeSession.EpochEndTimeSec);

	/// After the 'E' entry is set no more 'T' entries can be added through hourly interrupt

	/// Finalize offlineSession flash structure
	char * sessionData = calloc(1000,1);
	chargeSession_GetSessionAsString(sessionData);
	offlineSession_UpdateSessionOnFile(sessionData);
	free(sessionData);
}


void chargeSession_Clear()
{
	ESP_LOGI(TAG, "Clearing csResetSession file");
	esp_err_t clearErr = storage_clearSessionResetInfo();

	if (clearErr != ESP_OK)
	{
		ESP_LOGE(TAG, "storage_clearSessionResetInfo() failed: %d", clearErr);
	}

	ESP_LOGI(TAG, "Clearing chargeSession");
	memset(&chargeSession, 0, sizeof(chargeSession));

	strcpy(sidOrigin, "     ");
	hasNewSessionIdFromCloud = false;
	hasReceivedStartChargingCommand = false;
}


void chargeSession_SetAuthenticationCode(char * idAsString)
{
	strcpy(chargeSession.AuthenticationCode, idAsString);
}


void chargeSession_ClearAuthenticationCode()
{
	memset(chargeSession.AuthenticationCode, 0, sizeof(chargeSession.AuthenticationCode));
}


void chargeSession_SetStoppedByRFID(bool stoppedByRFID)
{
	if(chargeSession.SessionId[0] != '\0')
		chargeSession.StoppedByRFID = stoppedByRFID;
}

/*void chargeSession_SetEnergy(float energy)
{
	energy = roundf(energy * 1000) / 1000;
	chargeSession.Energy = energy;
}*/


void chargeSession_SetOCMF(char * OCMDString)
{
	chargeSession.SignedSession = OCMDString;
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
	cJSON_AddNumberToObject(CompletedSessionObject, "Energy", chargeSession.Energy);
	cJSON_AddStringToObject(CompletedSessionObject, "StartDateTime", chargeSession.StartDateTime);
	cJSON_AddStringToObject(CompletedSessionObject, "EndDateTime", chargeSession.EndDateTime);
	cJSON_AddBoolToObject(CompletedSessionObject, "ReliableClock", chargeSession.ReliableClock);
	cJSON_AddBoolToObject(CompletedSessionObject, "StoppedByRFID", chargeSession.StoppedByRFID);
	cJSON_AddStringToObject(CompletedSessionObject, "AuthenticationCode", chargeSession.AuthenticationCode);
	cJSON_AddStringToObject(CompletedSessionObject, "SignedSession", chargeSession.SignedSession);

	char *buf = cJSON_PrintUnformatted(CompletedSessionObject);

	strcpy(message, buf);

	ESP_LOGI(TAG, "Made CompletedSessionObject");

	cJSON_Delete(CompletedSessionObject);
	free(buf);

	return 0;
}


esp_err_t chargeSession_SaveSessionResetInfo()
{
	ESP_LOGI(TAG, "Saving resetSession: %s, Start: %s - %d,  %f W, %s", chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
	esp_err_t err = storage_SaveSessionResetInfo(chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
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

		err = storage_ReadSessionResetInfo(chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
		if (err != ESP_OK)
		{
			return err;
		}

		if(strlen(chargeSession.SessionId) == 36)
		{
			ESP_LOGI(TAG, "Loaded resetSession: %s, Start: %s - %d,  %f W, %s", chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
		}
		else
		{
			ESP_LOGI(TAG, "No SessionId, found on flash. Starting with clean session");
		}
	}
	return err;
}


bool chargeSession_IsAuthenticated()
{
	if(chargeSession.AuthenticationCode[0] != '\0')
		return true;
	else
		return false;
}

void chargeSession_SetReceivedStartChargingCommand()
{
	hasReceivedStartChargingCommand = true;
}
