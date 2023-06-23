#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "chargeSession.h"
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
#include "../components/zaptec_cloud/include/zaptec_cloud_observations.h"



static const char *TAG = "CHARGESESSION  ";

static struct ChargeSession chargeSession = {0};

static bool hasNewSessionIdFromCloud = false;

static char * basicOCMF = "OCMF|{}";

static char sidOrigin[6] = {0};

static bool isCarConnected = false;

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

///When OCPP in cloud receives a RemoteStopTransaction, it leads to a user UUID reset and Session reset command being sent from cloud.
///To avoid the user UUID being held in memory despite the first user UUID reset command, this flag should be set.
static bool UUIDClearedFlag = false;
void SetUUIDFlagAsCleared()
{
	UUIDClearedFlag = true;
}

static char holdAuthenticationCode[41] = {0};
void chargeSession_HoldUserUUID()
{
	if((chargeSession.AuthenticationCode[0] != '\0') && (UUIDClearedFlag == false))
	{
		ESP_LOGE(TAG,"Holding userUUID in mem");
		strcpy(holdAuthenticationCode, chargeSession.AuthenticationCode);
	}
}

char * sessionSession_GetHeldUserUUID()
{
	return holdAuthenticationCode;
}

bool sessionSession_IsHoldingUserUUID()
{
	if(holdAuthenticationCode[0] != '\0')
		return true;
	else
		return false;
}

void chargeSession_ClearHeldUserUUID()
{
	strcpy(holdAuthenticationCode, "");
}

/*
 * This function returns the format required for CompletedSession
 */
/*void GetUTCTimeString(char * timeString, time_t *epochSec, uint32_t *epochUsec)
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
}*/


static void ChargeSession_Set_StartTime()
{
	//bool useHoldTimeStamp = cloud_observation_UseAndClearHoldRequestTimestamp();

	struct HoldSessionStartTime *timeStruct = cloud_observation_GetTimeStruct();

	///If request has been sent with a timestamp, use this timestamp for session start
	if(timeStruct->usedInRequest == true)
	{
		///Use the exact time info that was used for first REQUEST to cloud as CompletedSession StartDateTime

		strcpy(chargeSession.StartDateTime, timeStruct->timeString);
		chargeSession.EpochStartTimeSec = timeStruct->holdEpochSec;
		chargeSession.EpochStartTimeUsec = timeStruct->holdEpochUsec;
		timeStruct->usedInSession = true;

		ESP_LOGW(TAG, "Using startTime from REQUESTING in SESSION: %s", chargeSession.StartDateTime);
	}
	else
	{
		/// No request timestamp to available, make new timestamp(offline or standalone)

		GetUTCTimeString(chargeSession.StartDateTime, &chargeSession.EpochStartTimeSec, &chargeSession.EpochStartTimeUsec);

		/// Set the startTime to be used by first requesting message in order for Cloud to be happy.
		cloud_observation_SetTimeStruct(chargeSession.StartDateTime, chargeSession.EpochStartTimeSec, chargeSession.EpochStartTimeUsec, true);

		ESP_LOGW(TAG, "Made startTime in SESSION for use in REQUESTING: %s", chargeSession.StartDateTime);

	}
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

		//Ensure we reply the SessionId every time Cloud sends it to charger
		hasNewSessionIdFromCloud = true;

		return 1;
	}

	else
	{
		offlineSession_AppendLogString(sessionIdFromCloud);
	}

	if(strlen(chargeSession.SessionId) > 0)
	{
		ESP_LOGW(TAG, "SessionId was already set: %s. Overwriting.", chargeSession.SessionId);
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
	chargeSession_SaveUpdatedSession();

	return 0;
}

void chargeSession_CheckIfLastSessionIncomplete()
{
	/// Check if sessionId is not set
	if((chargeSession.SessionId[0] == '\0'))
	{
		offlineSession_CheckIfLastLessionIncomplete(&chargeSession);

		///...Check if read from file
		if(chargeSession.SessionId[0] != '\0')
		{
			///If contains startDateTime
			if(chargeSession.StartDateTime[0] != '\0')
			{
				/// Set the startTime to be used by first requesting message in order for Cloud to be happy.
				cloud_observation_SetTimeStruct(chargeSession.StartDateTime, chargeSession.EpochStartTimeSec, chargeSession.EpochStartTimeUsec, true);

				ESP_LOGW(TAG, "Read startTime from SESSION-FILE for use in REQUESTING: %s", chargeSession.StartDateTime);
			}
		}
	}
}

/// For remote functional testing of the file correction feature
static bool testFileCorrection = false;
void chargeSession_SetTestFileCorrection()
{
	testFileCorrection = true;
}

///Flag if the file system error has occured
static bool sessionFileError = false;
bool chargeSession_GetFileError()
{
	//Clear on read
	bool tmp = sessionFileError;
	sessionFileError = false;
	return tmp;
}

static double startAcc = 0.0;
void chargeSession_Start()
{
	ESP_LOGI(TAG, "* STARTING SESSION *");

	if((strlen(chargeSession.SessionId) == 36))// && (readErr == ESP_OK))
	{
		ESP_LOGI(TAG, "chargeSession_Start() using uncompleted Session from flash");
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

		char * sessionData = calloc(1000,1);
		chargeSession_GetSessionAsString(sessionData);

		sessionFileError = false;

		esp_err_t saveErr = offlineSession_SaveSession(sessionData);

		//Check to see if the file could not be created
		if((saveErr == -2) || (testFileCorrection == true))
		{
			testFileCorrection = false;
			sessionFileError = true;

			ESP_LOGW(TAG, "FILE ERROR");
			offlineSession_ClearDiagnostics();
			offlineSession_eraseAndRemountPartition();
			saveErr = offlineSession_SaveSession(sessionData);
		}

		free(sessionData);

		if (saveErr != ESP_OK)
		{
			ESP_LOGE(TAG, "offlineSession_SaveSession() failed: %d", saveErr);
		}

		OCMF_CompletedSession_StartStopOCMFLog('B', chargeSession.EpochStartTimeSec);

	}

	startAcc = storage_update_accumulated_energy(0.0);
}


float chargeSession_GetEnergy()
{
	return chargeSession.Energy;
}

void chargeSession_UpdateEnergy()
{
	if(strlen(chargeSession.SessionId) > 0)
	{
		float energy = MCU_GetEnergy();

		if(energy > 0.001)
		{
			energy = roundf(energy * 10000) / 10000.0; /// round to 3rd decimal to remove unnecessary desimals

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
	chargeSession_UpdateEnergy();
	GetUTCTimeString(chargeSession.EndDateTime, &chargeSession.EpochEndTimeSec, &chargeSession.EpochEndTimeUsec);

	ESP_LOGI(TAG, "End time is: %s (%d.%d)", chargeSession.EndDateTime, (uint32_t)chargeSession.EpochEndTimeSec, chargeSession.EpochEndTimeUsec);


	/// Create the 'E' message
	OCMF_CompletedSession_StartStopOCMFLog('E', chargeSession.EpochEndTimeSec);

	/// After the 'E' entry is set no more 'T' entries can be added through hourly interrupt

	double endAcc = OCMF_GetLastAccumulated_Energy();
	double accDiff = endAcc - startAcc;
	if(fabs(accDiff - chargeSession.Energy) < 0.001)
		ESP_LOGW(TAG, "**** ACC-DIFF: %f - %f = %f vs %f **** OK", endAcc, startAcc, accDiff, chargeSession.Energy);
	else
		ESP_LOGE(TAG, "#### ACC-DIFF: %f - %f = %f vs %f #### FAIL", endAcc, startAcc, accDiff, chargeSession.Energy);

	/// Finalize offlineSession flash structure
	char * sessionData = calloc(1000,1);
	chargeSession_GetSessionAsString(sessionData);
	offlineSession_UpdateSessionOnFile(sessionData, false);


	offlineSession_SetSessionFileInactive();
	free(sessionData);

	cloud_observation_ClearTimeStruct();
}


void chargeSession_Clear()
{
	ESP_LOGI(TAG, "Clearing chargeSession");
	memset(&chargeSession, 0, sizeof(chargeSession));

	strcpy(sidOrigin, "     ");
	hasNewSessionIdFromCloud = false;

	ESP_LOGI(TAG, "* FINALIZED AND CLEARED SESSION *");
}

//Query if the SessionId source is local
bool chargeSession_IsLocalSession()
{
	if(sidOrigin[0] == 'l')
		return true;
	else
		return false;
}

void chargeSession_SetAuthenticationCode(char * idAsString)
{
	strcpy(chargeSession.AuthenticationCode, idAsString);

	/// Once a new auth string is registered, allow holding it during session reset from cloud
	UUIDClearedFlag = false;
}

char* chargeSession_GetAuthenticationCode()
{
	return chargeSession.AuthenticationCode;
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

void chargeSession_SetEnergyForTesting(float e)
{
	chargeSession.Energy = e;
}

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

	double energyAsDouble  = round(chargeSession.Energy * 10000) / 10000.0;

	//cJSON_AddNumberToObject(CompletedSessionObject, "Energy", chargeSession.Energy);
	cJSON_AddNumberToObject(CompletedSessionObject, "Energy", energyAsDouble);
	cJSON_AddStringToObject(CompletedSessionObject, "StartDateTime", chargeSession.StartDateTime);
	cJSON_AddStringToObject(CompletedSessionObject, "EndDateTime", chargeSession.EndDateTime);
	cJSON_AddBoolToObject(CompletedSessionObject, "ReliableClock", chargeSession.ReliableClock);
	cJSON_AddBoolToObject(CompletedSessionObject, "StoppedByRFID", chargeSession.StoppedByRFID);
	cJSON_AddStringToObject(CompletedSessionObject, "AuthenticationCode", chargeSession.AuthenticationCode);
	cJSON_AddStringToObject(CompletedSessionObject, "SignedSession", chargeSession.SignedSession);

	char *buf = cJSON_PrintUnformatted(CompletedSessionObject);

	strcpy(message, buf);

	ESP_LOGI(TAG, "Made CompletedSessionObject %d", strlen(message));

	cJSON_Delete(CompletedSessionObject);
	free(buf);

	return 0;
}


/// When an RFID-tag is validated, call this to update the session on file
esp_err_t chargeSession_SaveUpdatedSession()
{

	char * sessionData = calloc(1000,1);
	chargeSession_GetSessionAsString(sessionData);
	offlineSession_UpdateSessionOnFile(sessionData, false);
	free(sessionData);

	/*ESP_LOGI(TAG, "Saving resetSession: %s, Start: %s - %d,  %f W, %s", chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
	esp_err_t err = storage_SaveSessionResetInfo(chargeSession.SessionId, chargeSession.StartDateTime, chargeSession.unixStartTime, chargeSession.Energy, chargeSession.AuthenticationCode);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "chargeSession_SaveSessionResetInfo() failed: %d", err);*/

	return ESP_OK;
}

/*esp_err_t chargeSession_ReadSessionResetInfo()
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
}*/


bool chargeSession_IsAuthenticated()
{
	if(chargeSession.AuthenticationCode[0] != '\0')
		return true;
	else
		return false;
}

bool chargeSession_HasSessionId()
{
	if(chargeSession.SessionId[0] != '\0')
		return true;
	else
		return false;
}


