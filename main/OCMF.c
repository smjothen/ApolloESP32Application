#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "stddef.h"
#include "OCMF.h"
#include "i2cDevices.h"
#include "../components/ntp/zntp.h"
#include "chargeSession.h"
#include "cJSON.h"
#include "string.h"
#include "storage.h"
#include "protocol_task.h"
#include "offlineSession.h"
#include <math.h>
#include "zaptec_cloud_observations.h"

static const char *TAG = "OCMF           ";

static char * formatVersion = "1.0";

static char * OCMFLogEntryString = NULL;

static SemaphoreHandle_t ocmf_lock;
static TickType_t lock_timeout = pdMS_TO_TICKS(1000*5);
cJSON * OCMF_AddElementToOCMFLog_no_lock(char *tx,  time_t time_in, double energy_in);

static double valueB = 0.0;
static double valueE = 0.0;

void OCMF_Init()
{
	OCMFLogEntryString = calloc(LOG_STRING_SIZE, 1);
	ocmf_lock = xSemaphoreCreateMutex();
	xSemaphoreGive(ocmf_lock);
}

static double previousEnergyMax = 0.0;
static double accumulated_energy = 0.0;

double OCMF_GetLastAccumulated_Energy()
{
	return accumulated_energy;
}

double get_accumulated_energy(){

	float dspic_session_energy_max = chargeSession_GetEnergy();

	ESP_LOGI(TAG, "dspic max %f (session: %f) opMode %i", dspic_session_energy_max, MCU_GetEnergy(), MCU_GetChargeOperatingMode());

	if(previousEnergyMax > dspic_session_energy_max){
		storage_update_accumulated_energy(0.0);
		ESP_LOGI(TAG, "previousEnergyMax (%f > %f) dspic_session_energy_max - passing 0.0 max value to STORAGE", previousEnergyMax, dspic_session_energy_max);
	}

	double accumulated_energy_tmp = storage_update_accumulated_energy(dspic_session_energy_max);

	accumulated_energy_tmp = round(accumulated_energy_tmp * 1000) / 1000;/// Rounding to 3 decimal places

	previousEnergyMax = dspic_session_energy_max;

	/// Ensure async reading can not get intermediately calculated value;
	accumulated_energy = accumulated_energy_tmp;

	return accumulated_energy;
}


int _OCMF_SignedMeterValue_CreateNewOCMFMessage(char * newMessage, char * time_buffer, double energy)
{
	cJSON *OCMFObject = cJSON_CreateObject();
	if(OCMFObject == NULL){return -10;}

	cJSON_AddStringToObject(OCMFObject, "FV", formatVersion);							//FormatVersion
	cJSON_AddStringToObject(OCMFObject, "GI", "ZAPTEC GO");								//GatewayIdentification
	cJSON_AddStringToObject(OCMFObject, "GS", i2cGetLoadedDeviceInfo().serialNumber);	//GatewaySerial
	cJSON_AddStringToObject(OCMFObject, "GV", GetSoftwareVersion());					//GatewayVersion
	cJSON_AddStringToObject(OCMFObject, "PG", "F1");			//Pagination(class)

	cJSON * readerArray = cJSON_CreateArray();
	cJSON * readerObject = cJSON_CreateObject();

	cJSON_AddStringToObject(readerObject, "TM", time_buffer);	//TimeAndSyncState
	cJSON_AddNumberToObject(readerObject, "RV", energy);	//ReadingValue
	cJSON_AddStringToObject(readerObject, "RI", "1-0:1.8.0");	//ReadingIdentification(OBIS-code)
	cJSON_AddStringToObject(readerObject, "RU", "kWh");			//ReadingUnit
	cJSON_AddStringToObject(readerObject, "RT", "AC");			//ReadingCurrentType
	cJSON_AddStringToObject(readerObject, "ST", "G");			//MeterState

	cJSON_AddItemToArray(readerArray, readerObject);
	cJSON_AddItemToObject(OCMFObject, "RD", readerArray);

	char *buf = cJSON_PrintUnformatted(OCMFObject);

	strcpy(newMessage, "OCMF|");
	strcpy(newMessage+strlen(newMessage), buf);

	//ESP_LOGW(TAG, "OCMF: %i: %s", strlen(buf), buf);

	cJSON_Delete(OCMFObject);

	free(buf);

	return 0;
}

int OCMF_SignedMeterValue_CreateNewOCMFMessage(char * newMessage, time_t *time_out, double *energy_out){
	char timeBuffer[50] = {0};
	zntp_GetSystemTime(timeBuffer, time_out);
	*energy_out = get_accumulated_energy();

	return _OCMF_SignedMeterValue_CreateNewOCMFMessage(newMessage, timeBuffer, *energy_out);

}

int  OCMF_SignedMeterValue_CreateMessageFromLog(char *new_message, time_t time_in, double energy_in){
	char time_buffer[50] = {0};
	zntp_format_time(time_buffer, time_in);

	return _OCMF_SignedMeterValue_CreateNewOCMFMessage(new_message, time_buffer, energy_in);
}


/************************************************************************************/
static cJSON *logRoot = NULL;
static cJSON * logReaderArray = NULL;

static bool energyFaultFlag = false;
bool OCMP_GetEnergyFaultFlag()
{
	bool tmp = energyFaultFlag;
	energyFaultFlag = false;
	return tmp;
}

esp_err_t OCMF_CompletedSession_CreateNewMessageFile(int oldestFile, char * messageString)
{
	if( xSemaphoreTake( ocmf_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain ocmf lock during finalize");
		return -1;
	}else{
		ESP_LOGI(TAG, "got ocmf lock OCMF_FinalizeOCMFLog");
	}


	if(oldestFile < 0)
	{
		xSemaphoreGive(ocmf_lock);
		return ESP_ERR_NOT_FOUND;
	}

	/// 1. First get cJSON ChargeSession from file
	cJSON *CompletedSessionObject = offlineSession_ReadChargeSessionFromFile(oldestFile);
	if(CompletedSessionObject == NULL)
	{
		xSemaphoreGive(ocmf_lock);
		offlineSession_AppendLogString("3 CS_Object == NULL");
		return ESP_ERR_NOT_FOUND;
	}

	offlineSession_AppendLogString("3 CS_Object OK");

	/// 2. ..then get OCMF log entires
	memset(OCMFLogEntryString, 0, LOG_STRING_SIZE);

	logRoot = cJSON_CreateObject();

	cJSON_AddStringToObject(logRoot, "FV", formatVersion);							//FormatVersion
	cJSON_AddStringToObject(logRoot, "GI", "ZAPTEC GO");							//GatewayIdentification
	cJSON_AddStringToObject(logRoot, "GS", i2cGetLoadedDeviceInfo().serialNumber);	//GatewaySerial
	cJSON_AddStringToObject(logRoot, "GV", GetSoftwareVersion());					//GatewayVersion
	cJSON_AddStringToObject(logRoot, "PG", "T1");									//Pagination(class)


	//Array to hold all hourly SignedSession energy elements
	cJSON *logReaderArray = offlineSession_GetSignedSessionFromActiveFile(oldestFile);

	if(logReaderArray != NULL)
	{
		ESP_LOGI(TAG, "CompletedSession OCMF Array size: %i: ", cJSON_GetArraySize(logReaderArray));

		double sessEnergy = cJSON_GetObjectItem(CompletedSessionObject,"Energy")->valuedouble;
		double signedEnergy = GetEnergySigned();
		double energyDiff = fabs(sessEnergy - signedEnergy);
		if(energyDiff >= 0.0001)
			ESP_LOGW(TAG, "1#### Sess: %f vs Signed: %f -> Diff: %f #### FAILED", sessEnergy, signedEnergy, energyDiff);
		else
			ESP_LOGI(TAG, "1**** Sess: %f vs Signed: %f -> Diff: %f **** OK", sessEnergy, signedEnergy, energyDiff);

		/// Compensate for rounding error by rounding sessionEnergy up or down to match signed value difference
		/// If case (0.0014 vs 0.002) normal rounding would give different result
		if(sessEnergy > signedEnergy)
			sessEnergy = floor(sessEnergy * 1000) / 1000.0;
		else if(sessEnergy < signedEnergy)
			sessEnergy = ceil(sessEnergy * 1000) / 1000.0;

		energyDiff = fabs(sessEnergy - signedEnergy);
		if(energyDiff >= 0.0001)
		{
			ESP_LOGE(TAG, "2#### Sess: %f vs Signed: %f -> Diff: %f #### FAILED", sessEnergy, signedEnergy, energyDiff);
			energyFaultFlag = true;
		}
		else
		{
			ESP_LOGI(TAG, "2**** Sess: %f vs Signed: %f -> Diff: %f **** OK", sessEnergy, signedEnergy, energyDiff);
		}

		/// Update the rounded session energy
		cJSON_GetObjectItem(CompletedSessionObject,"Energy")->valuedouble = sessEnergy;

		//ESP_LOGW(TAG, "CompletedSession B: %s", cJSON_PrintUnformatted(CompletedSessionObject));
		//ESP_LOGW(TAG, "IsBool B: %i", cJSON_IsBool(cJSON_GetObjectItem(CompletedSessionObject,"ReliableClock")));

		/// If Endtime is not set(disconnected while powered off), clear ReliableClock
		int edtLength = strlen(cJSON_GetObjectItem(CompletedSessionObject,"EndDateTime")->valuestring);
		if(edtLength < 27)
		{
			cJSON_ReplaceItemInObject(CompletedSessionObject, "ReliableClock", cJSON_CreateBool(false));
			ESP_LOGW(TAG, "Cleared ReliableClock");

			/// Must set an EndDateTime(for Cloud to parse) even tough the time is incorrect. Set time of reporting.
			char lastEdt[33];
			GetUTCTimeString(lastEdt, NULL, NULL);
			cJSON_ReplaceItemInObject(CompletedSessionObject, "EndDateTime", cJSON_CreateString(lastEdt));
			ESP_LOGE(TAG, "Set final EDT %s", lastEdt);
		}
		//ESP_LOGW(TAG, "EndDateTime length: %i", edtLength);

		cJSON_AddItemToObject(logRoot, "RD", logReaderArray);

		char *buf = cJSON_PrintUnformatted(logRoot);

		//ESP_LOGW(TAG, "CompletedSession E: %s", cJSON_PrintUnformatted(CompletedSessionObject));

		strcpy(OCMFLogEntryString, "OCMF|");
		strcpy(OCMFLogEntryString+strlen(OCMFLogEntryString), buf);

		//ESP_LOGW(TAG, "OCMF: %i: %s", strlen(buf), buf);

		cJSON_Delete(logRoot);

		logRoot = NULL;
		logReaderArray = NULL;

		free(buf);

		cJSON_DeleteItemFromObject(CompletedSessionObject, "SignedSession");
		//ESP_LOGW(TAG, "CompletedSession2: %s", cJSON_PrintUnformatted(CompletedSessionObject));
		cJSON_AddStringToObject(CompletedSessionObject, "SignedSession", OCMFLogEntryString);
		//cJSON_GetObjectItem(CompletedSessionObject,"SignedSession")->valuestring = OCMFLogEntryString;

		//ESP_LOGW(TAG, "CompletedSession3: %s", cJSON_PrintUnformatted(CompletedSessionObject));
	}
	else
	{
		ESP_LOGW(TAG, "Could not finalize SignedValues from flash");
	}

	///3. Create the full message string
	char *buf = cJSON_PrintUnformatted(CompletedSessionObject);

	strcpy(messageString, buf);

	ESP_LOGI(TAG, "\r\nOCMF: %i:: %s\r\n", strlen(messageString), messageString);

	ESP_LOGI(TAG, "Made CompletedSessionObject");

	cJSON_Delete(CompletedSessionObject);
	free(buf);

	//For testing maximum message size
	//
	//int i;
	//for (i = 0; i < 99; i++)
	//	OCMF_AddElementToOCMFLog("T", "G");

	xSemaphoreGive(ocmf_lock);

	return ESP_OK;
}


void OCMF_CompletedSession_StartStopOCMFLog(char label, time_t startTimeSec)
{
	double energyAtStart = get_accumulated_energy();
	OCMF_CompletedSession_AddElementToOCMFLog(label, startTimeSec, energyAtStart);
}


cJSON * OCMF_AddElementToOCMFLog_no_lock(char *tx, time_t time_in, double energy_in)
{

	if(logReaderArray != NULL)
	{
		int arrayLength = cJSON_GetArraySize(logReaderArray);

		//Allow max 100 entries including end message to limit message size.
		if((arrayLength < 99) || ((arrayLength == 99) && (tx[0] == 'E')))
		{
			cJSON * logArrayElement = cJSON_CreateObject();
			char timeBuffer[50] = {0};
			zntp_format_time(timeBuffer, time_in);
			//zntp_GetSystemTime(timeBuffer, NULL);

			cJSON_AddStringToObject(logArrayElement, "TM", timeBuffer);	//TimeAndSyncState
			cJSON_AddStringToObject(logArrayElement, "TX", tx);	//Message status (B, T, E)
			cJSON_AddNumberToObject(logArrayElement, "RV", energy_in);//get_accumulated_energy());	//ReadingValue
			cJSON_AddStringToObject(logArrayElement, "RI", "1-0:1.8.0");	//ReadingIdentification(OBIS-code)
			cJSON_AddStringToObject(logArrayElement, "RU", "kWh");			//ReadingUnit
			cJSON_AddStringToObject(logArrayElement, "RT", "AC");			//ReadingCurrentType
			cJSON_AddStringToObject(logArrayElement, "ST", "G");			//MeterState

			cJSON_AddItemToArray(logReaderArray, logArrayElement);

			ESP_LOGI(TAG, "OCMF Array size: %i: ", cJSON_GetArraySize(logReaderArray));
		}
		else
		{
			ESP_LOGW(TAG, "MAX OCMF Array size reached");
		}
	}
	return logReaderArray;
}


static bool energyFault = false;
bool OCMF_GetEnergyFault()
{
	bool tmp = energyFault;
	energyFault = false;
	return tmp;
}

void OCMF_CompletedSession_AddElementToOCMFLog(char tx, time_t time_in, double energy_in)
{
	//Add to file log
	offlineSession_append_energy(tx, time_in, energy_in);

	/// This is diagnostics output to show if there is an energy mismatch between
	/// End of one session and Beginning of the next
	if(tx == 'E')
	{
		valueB = 0.0;
		valueE = energy_in;
	}
	else if((tx == 'B') && (valueE != 0.0))
	{
		valueB = energy_in;

		if(valueB == valueE)
		{
			ESP_LOGI(TAG, "");
			ESP_LOGI(TAG, "*****************  OK: %f == %f (E=B)  *****************", valueE, valueB);
			ESP_LOGI(TAG, "");
		}
		else
		{
			ESP_LOGE(TAG, "");
			ESP_LOGE(TAG, "*****************  FAIL: %f != %f (E!=B) ****************", valueE, valueB);
			ESP_LOGE(TAG, "");
			energyFault = true;
		}
	}

}

/*int OCMF_CompletedSession_FinalizeOCMFLog()
{
	if( xSemaphoreTake( ocmf_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain ocmf lock during finalize");
		return -1;
	}else{
		ESP_LOGI(TAG, "got ocmf lock OCMF_FinalizeOCMFLog");
	}


	///Save the entry to the offline log in case CompletedSession is not transmitted successfully
	//offlineSession_append_energy('E', endTime, endEnergy);

	if(logReaderArray != NULL)
	{
		//double endEnergy = get_accumulated_energy();

		///Save the entry to the offline log in case CompletedSession is not transmitted successfully
		//offlineSession_append_energy('E', endTime, endEnergy);

		///Build the OCMF structure for sending instantly
		//char label = 'E';
		//OCMF_AddElementToOCMFLog_no_lock(&label, endTime, endEnergy);

		cJSON_AddItemToObject(logRoot, "RD", logReaderArray);

		char *buf = cJSON_PrintUnformatted(logRoot);

		strcpy(logString, "OCMF|");
		strcpy(logString+strlen(logString), buf);

		ESP_LOGW(TAG, "OCMF: %i: %s", strlen(buf), buf);

		cJSON_Delete(logRoot);

		logRoot = NULL;
		logReaderArray = NULL;

		free(buf);
		chargeSession_SetOCMF(logString);
	}
	else
	{
		ESP_LOGW(TAG, "Nothing to finalize");
	}

	xSemaphoreGive(ocmf_lock);
	return 0;
}*/


/*int OCMF_MakeAndSaveNewOfflineSessionEntry(time_t time, double energy)
{
	if( xSemaphoreTake( ocmf_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain ocmf lock during finalize");
		return -1;
	}else{
		ESP_LOGI(TAG, "got ocmf lock OCMF_FinalizeOCMFLog");
	}

	if(logReaderArray != NULL)
	{
		OCMF_AddElementToOCMFLog_no_lock("T", time, energy);

		cJSON_AddItemToObject(logRoot, "RD", logReaderArray);

		char *buf = cJSON_PrintUnformatted(logRoot);

		strcpy(logString, "OCMF|");
		strcpy(logString+strlen(logString), buf);

		ESP_LOGW(TAG, " After adding OCMF: %i: %s", strlen(buf), buf);

		//cJSON_Delete(logRoot);

		//logRoot = NULL;
		//logReaderArray = NULL;

		free(buf);
		chargeSession_SetOCMF(logString);
	}
	else
	{
		ESP_LOGW(TAG, "Nothing to add");
	}

	xSemaphoreGive(ocmf_lock);
	return 0;
}*/
