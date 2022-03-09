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
#include <math.h>

static const char *TAG = "OCMF     ";

static char * formatVersion = "1.0";

static char * logString = NULL;

static SemaphoreHandle_t ocmf_lock;
static TickType_t lock_timeout = pdMS_TO_TICKS(1000*5);
cJSON * OCMF_AddElementToOCMFLog_no_lock(const char * const tx, const char * const st);


void OCMF_Init()
{
	logString = calloc(LOG_STRING_SIZE, 1);
	ocmf_lock = xSemaphoreCreateMutex();
	xSemaphoreGive(ocmf_lock);
}

double get_accumulated_energy(){
	float dspic_session_energy = MCU_GetEnergy();

	float max = MCU_GetMaximumEnergy();

	if((max>0.0) && (dspic_session_energy < max)){
		MCU_ClearMaximumEnergy();
		ESP_LOGI(TAG, "detected dspic energy reset (%f, %f), passing max value to STORAGE",
			dspic_session_energy, max
		);
		dspic_session_energy = max;
	}

	double accumulated_energy = storage_update_accumulated_energy(dspic_session_energy);

	accumulated_energy = round(accumulated_energy * 1000) / 1000;/// Rounding to 3 decimal places

	return accumulated_energy;
}

int _OCMF_CreateNewOCMFMessage(char * newMessage, char * time_buffer, double energy)
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

	ESP_LOGW(TAG, "OCMF: %i: %s", strlen(buf), buf);

	cJSON_Delete(OCMFObject);

	free(buf);

	return 0;
}

int OCMF_CreateNewOCMFMessage(char * newMessage, time_t *time_out, double *energy_out){
	char timeBuffer[50] = {0};
	zntp_GetSystemTime(timeBuffer, time_out);
	*energy_out = get_accumulated_energy();

	return _OCMF_CreateNewOCMFMessage(newMessage, timeBuffer, *energy_out);

}

int  OCMF_CreateMessageFromLog(char *new_message, time_t time_in, double energy_in){
	char time_buffer[50] = {0};
	zntp_format_time(time_buffer, time_in);

	return _OCMF_CreateNewOCMFMessage(new_message, time_buffer, energy_in);
}


static cJSON *logRoot = NULL;
static cJSON * logReaderArray = NULL;
//static cJSON *logArrayElement = NULL;


char * OCMF_CreateNewOCMFLog()
{
	memset(logString, 0, LOG_STRING_SIZE);

	if( xSemaphoreTake( ocmf_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain ocmf lock during log create");
		goto err;
	}else{
		ESP_LOGI(TAG, "got ocmf lock OCMF_CreateNewOCMFLog");
	}


	//if(logRoot != NULL){return -9;}

	logRoot = cJSON_CreateObject();
	//if(logRoot == NULL){return -10;}

	cJSON_AddStringToObject(logRoot, "FV", formatVersion);							//FormatVersion
	cJSON_AddStringToObject(logRoot, "GI", "ZAPTEC GO");								//GatewayIdentification
	cJSON_AddStringToObject(logRoot, "GS", i2cGetLoadedDeviceInfo().serialNumber);	//GatewaySerial
	cJSON_AddStringToObject(logRoot, "GV", GetSoftwareVersion());					//GatewayVersion
	cJSON_AddStringToObject(logRoot, "PG", "T1");			//Pagination(class)

	logReaderArray = cJSON_CreateArray();
	//logArrayElement = cJSON_CreateObject();

	/*char timeBuffer[50] = {0};
	zntp_GetSystemTime(timeBuffer);

	cJSON_AddStringToObject(logArrayElement, "TM", timeBuffer);	//TimeAndSyncState
	cJSON_AddNumberToObject(logArrayElement, "TX", "B");	//Message status (B, T, E)
	cJSON_AddNumberToObject(logArrayElement, "RV", chargeSession_Get().Energy * 0.001);	//ReadingValue
	cJSON_AddStringToObject(logArrayElement, "RI", "1-0:1.8.0");	//ReadingIdentification(OBIS-code)
	cJSON_AddStringToObject(logArrayElement, "RU", "kWh");			//ReadingUnit
	cJSON_AddStringToObject(logArrayElement, "RT", "AC");			//ReadingCurrentType
	cJSON_AddStringToObject(logArrayElement, "ST", "G");			//MeterState*/

	logReaderArray = OCMF_AddElementToOCMFLog_no_lock("B", "G");
	//logReaderArray = OCMF_AddElementToOCMFLog("B", "G");
	//logReaderArray = OCMF_AddElementToOCMFLog("B", "G");

	//cJSON_AddItemToObject(logRoot, "RD", logReaderArray);

	/*char *buf = cJSON_PrintUnformatted(logRoot);

	strcpy(logString, "OCMF|");
	strcpy(logString+strlen(logString), buf);

	ESP_LOGW(TAG, "OCMF: %i: %s", strlen(buf), buf);

	free(buf);*/

	//For testing maximum message size
	/*
	int i;
	for (i = 0; i < 99; i++)
		OCMF_AddElementToOCMFLog("T", "G");
	*/

	xSemaphoreGive(ocmf_lock);

	err:
	return logString;
}

cJSON * OCMF_AddElementToOCMFLog_no_lock(const char * const tx, const char * const st)
{

	if(logReaderArray != NULL)
	{
		int arrayLength = cJSON_GetArraySize(logReaderArray);

		//Allow max 100 entries including end message to limit message size.
		if((arrayLength < 99) || ((arrayLength == 99) && (tx[0] == 'E')))
		{
			cJSON * logArrayElement = cJSON_CreateObject();
			char timeBuffer[50] = {0};
			zntp_GetSystemTime(timeBuffer, NULL);

			cJSON_AddStringToObject(logArrayElement, "TM", timeBuffer);	//TimeAndSyncState
			cJSON_AddStringToObject(logArrayElement, "TX", tx);	//Message status (B, T, E)
			cJSON_AddNumberToObject(logArrayElement, "RV", get_accumulated_energy());	//ReadingValue
			cJSON_AddStringToObject(logArrayElement, "RI", "1-0:1.8.0");	//ReadingIdentification(OBIS-code)
			cJSON_AddStringToObject(logArrayElement, "RU", "kWh");			//ReadingUnit
			cJSON_AddStringToObject(logArrayElement, "RT", "AC");			//ReadingCurrentType
			cJSON_AddStringToObject(logArrayElement, "ST", st);			//MeterState

			cJSON_AddItemToArray(logReaderArray, logArrayElement);

			ESP_LOGW(TAG, "OCMF Array size: %i: ", cJSON_GetArraySize(logReaderArray));
		}
		else
		{
			ESP_LOGW(TAG, "MAX OCMF Array size reached");
		}
	}
	return logReaderArray;
}


cJSON * OCMF_AddElementToOCMFLog(const char * const tx, const char * const st)
{
	cJSON * result = NULL;

	if( xSemaphoreTake( ocmf_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain ocmf lock during element add");
		goto err;
	}else{
		ESP_LOGI(TAG, "got ocmf lock OCMF_AddElementToOCMFLog");
	}

	result = OCMF_AddElementToOCMFLog_no_lock(tx, st);

	xSemaphoreGive(ocmf_lock);
	err:
	return result;
}

int OCMF_FinalizeOCMFLog()
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
		OCMF_AddElementToOCMFLog_no_lock("E", "G");

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
}


int OCMF_MakeAndSaveNewOfflineSessionEntry()
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
		OCMF_AddElementToOCMFLog_no_lock("T", "G");

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
}
