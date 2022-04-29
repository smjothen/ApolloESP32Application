#include "offlineSession.h"

#define TAG "OFFLINE_LOG"

#include "esp_log.h"
#include "errno.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "base64.h"

#include "OCMF.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_protocol_serialisation.h"
#include "chargeSession.h"
#include "../components/ntp/zntp.h"

static const char *tmp_path = "/offs";
//static const char *log_path = "/offs/1.json";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

//static const int max_offline_session_files = 10;

#define FILE_VERSION_ADDR_0  		0L
#define FILE_SESSION_ADDR_2  		2L
#define FILE_SESSION_CRC_ADDR_996	996L
#define FILE_NR_OF_OCMF_ADDR_1000  	1000L
#define FILE_OCMF_START_ADDR_1004 	1004L

struct LogHeader {
    int start;
    int end;
    uint32_t crc; // for header, not whole file
    // dont keep version, use other file name for versioning
};


struct LogOCMFData {
	char label;
    int timestamp;
    double energy;
    uint32_t crc;
};

static bool offlineSessionOpen = false;

bool offlineSession_mount_folder()
{
    static bool mounted = false;

	if(mounted)
	{
		ESP_LOGI(TAG, "/offs already mounted");
		return mounted;
	}

    ESP_LOGI(TAG, "Mounting /offs");
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

	esp_err_t err = esp_vfs_fat_spiflash_mount(tmp_path, "files", &mount_config, &s_wl_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
		return mounted;
	}

	mounted = true;

	ESP_LOGI(TAG, "offs mounted");

	return mounted;
}



//int ensure_valid_header(FILE *fp, int *start_out, int *end_out){
//    struct LogHeader head_in_file = {0};
//    fseek(fp, 0, SEEK_SET);
//    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
//    int read_result = fread(&head_in_file, 1,sizeof(head_in_file),  fp);
//    ESP_LOGI(TAG, "header on disk %d %d %u (s:%d, res:%d)    <<<   ",
//    head_in_file.start, head_in_file.end, head_in_file.crc, sizeof(head_in_file), read_result);
//    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
//    perror("read perror: ");
//    uint32_t crc_in_file = head_in_file.crc;
//    head_in_file.crc = 0;
//
//    uint32_t calculated_crc = crc32_normal(0, &head_in_file, sizeof(head_in_file));
//
//    if(crc_in_file == calculated_crc){
//        ESP_LOGI(TAG, "Found valid header");
//        *start_out = head_in_file.start;
//        *end_out = head_in_file.end;
//    }else{
//        ESP_LOGE(TAG, "INVALID HEAD, staring log anew");
//
//        int new_header_result = update_header(fp, 0, 0);
//        *start_out = 0;
//        *end_out = 0;
//
//        if(new_header_result<0){
//            return -1;
//        }
//    }
//
//    return 0;
//
//}



/*
 * Find the file to use for a new session
 */
int offlineSession_FindLatestFile()
{
	int fileNo = 0;
	FILE *file;
	char buf[22] = {0};
	int fileCount = 0;

	for (fileNo = 0; fileNo < (100); fileNo++ )
	{
		sprintf(buf,"/offs/%d.bin", fileNo);

		file = fopen(buf, "r");
		if(file != NULL)
		{
			fclose(file);
			ESP_LOGW(TAG, "OfflineSession file: %d", fileNo);
			fileCount++;
		}
		else
		{
			//Use last existing file
			if(fileNo > 0)
				fileNo -= 1;

			ESP_LOGW(TAG, "Nr of OfflineSession files: %d. Using: %d", fileCount, fileNo);
			break;
		}
	}

	return fileNo;
}

/*
 * Find the oldest file to read, send, delete
 */
int offlineSession_FindOldestFile()
{
	int fileNo = 0;
	FILE *file;
	char buf[22] = {0};

	for (fileNo = 0; fileNo < 100; fileNo++ )
	{
		sprintf(buf,"/offs/%d.bin", fileNo);

		file = fopen(buf, "r");
		if(file != NULL)
		{
			fclose(file);
			ESP_LOGW(TAG, "OfflineSession file: %d", fileNo);
			return fileNo; ///Return the current file number
		}

	}

	return -1; //No files found
}


/*
 * Find the oldest file to read, send, delete
 */
int offlineSession_FindNrOfFiles()
{
	int fileNo = 0;
	FILE *file;
	char buf[22] = {0};
	int fileCount = 0;

	for (fileNo = 0; fileNo < 100; fileNo++ )
	{
		sprintf(buf,"/offs/%d.bin", fileNo);

		file = fopen(buf, "r");
		if(file != NULL)
		{
			fclose(file);
			fileCount++;
			ESP_LOGW(TAG, "OfflineSession found file: %d", fileNo);
		}

	}
	ESP_LOGW(TAG, "Nr of OfflineSession files: %d", fileCount);

	return fileCount;
}



volatile static int activeFileNumber = -1;
static char activePathString[22] = {0};
static FILE *sessionFile = NULL;

void offlineSession_UpdateSessionOnFile(char *sessionData)
{

	sessionFile = fopen(activePathString, "rb+");

	uint8_t fileVersion = 1;
	fseek(sessionFile, FILE_VERSION_ADDR_0, SEEK_SET);
	fwrite(&fileVersion, 1, 1, sessionFile);

	int sessionDataLen = strlen(sessionData);
	char * base64SessionData;// = calloc(500,1);
	size_t outLen = 0;
	base64SessionData = base64_encode(sessionData, sessionDataLen, &outLen);
	volatile int base64SessionDataLen = strlen(base64SessionData);

	ESP_LOGW(TAG,"%d: %s\n", strlen(sessionData), sessionData);
	ESP_LOGW(TAG,"%d: %s\n", strlen(base64SessionData), base64SessionData);

	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);
	fwrite(base64SessionData, base64SessionDataLen, 1, sessionFile);

	ESP_LOGW(TAG,"Write session: %i, %i:, %s", base64SessionDataLen, strlen(base64SessionData), base64SessionData);

	//Write CRC at the end of the block
	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);
	fseek(sessionFile, FILE_SESSION_CRC_ADDR_996, SEEK_SET);
	fwrite(&crcCalc, sizeof(uint32_t), 1, sessionFile);

	ESP_LOGW(TAG, "Session CRC:: 0x%X", crcCalc);



	/*fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);

//char * base64SessionData = calloc(1000-4, 1);
	memset(base64SessionData, 0, 996);
	fread(base64SessionData, 1000-4, 1, sessionFile);


	base64SessionDataLen = strlen(base64SessionData);

	volatile uint32_t crcReCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);

	ESP_LOGW(TAG,"Read session: %i, 0x%X:, %s", base64SessionDataLen, crcReCalc, base64SessionData);

	//ESP_LOGW(TAG,"Session CRC read control: 0x%X vs 0x%X: %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");
*/
	fclose(sessionFile);
	free(base64SessionData);
}


esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo)
{
	char buf[22] = {0};
	sprintf(buf,"/offs/%d.bin", fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		return ESP_FAIL;
	}

	uint8_t fileVersion = 0;
	fread(&fileVersion, 1, 1, sessionFile);
	ESP_LOGW(TAG, "File version: %d", fileVersion);

	/// Read session CRC
	fseek(sessionFile, FILE_SESSION_CRC_ADDR_996, SEEK_SET);
	uint32_t crcRead = 0;
	fread(&crcRead, sizeof(uint32_t), 1, sessionFile);

	/// Go to beginning before reading
	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);


	char * base64SessionData = calloc(1000-4, 1);
	fread(base64SessionData, 1000-4, 1, sessionFile);

	int readLen = strlen(base64SessionData);
	uint32_t crcCalc = 0;
	if(readLen <= 996)
		crcCalc = crc32_normal(0, base64SessionData, readLen);

	ESP_LOGW(TAG,"Session CRC read control: 0x%X vs 0x%X: %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");

	if(crcRead != crcCalc)
	{
		return ESP_ERR_INVALID_CRC;
	}

	/// Find first \0 element in the array, index = length of base64 encoded CompletedSession-structure
	int endIndex = 0;
	for (endIndex = 0; endIndex <= 999; endIndex++)
	{
		if(base64SessionData[endIndex] == '\0')
		{
			break;
		}
	}


	int base64SessionDataLen = endIndex+1;
	size_t outLen = 0;

	char *sessionDataCreated = (char*)base64_decode(base64SessionData, base64SessionDataLen, &outLen);

	printf("%d: %s\n", strlen(base64SessionData), base64SessionData);
	printf("%d: %.*s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

	struct ChargeSession chargeSessionFromFile = {0};

	cJSON* jsonSession = cJSON_Parse(sessionDataCreated);

	if(cJSON_HasObjectItem(jsonSession, "SessionId") &&
			cJSON_HasObjectItem(jsonSession, "Energy") &&
			cJSON_HasObjectItem(jsonSession, "StartDateTime") &&
			cJSON_HasObjectItem(jsonSession, "EndDateTime") &&
			cJSON_HasObjectItem(jsonSession, "ReliableClock") &&
			cJSON_HasObjectItem(jsonSession, "StoppedByRFID")&&
			cJSON_HasObjectItem(jsonSession, "AuthenticationCode"))
	{
		strncpy(chargeSessionFromFile.SessionId, 	cJSON_GetObjectItem(jsonSession,"SessionId")->valuestring, 37);
		chargeSessionFromFile.Energy = 				cJSON_GetObjectItem(jsonSession,"Energy")->valuedouble;
		strncpy(chargeSessionFromFile.StartDateTime,	cJSON_GetObjectItem(jsonSession,"StartDateTime")->valuestring, 32);
		strncpy(chargeSessionFromFile.EndDateTime,	cJSON_GetObjectItem(jsonSession,"EndDateTime")->valuestring, 32);

		if(cJSON_GetObjectItem(jsonSession,"ReliableClock")->valueint > 0)
			chargeSessionFromFile.ReliableClock = true;
		else
			chargeSessionFromFile.ReliableClock = false;

		if(cJSON_GetObjectItem(jsonSession,"StoppedByRFID")->valueint > 0)
			chargeSessionFromFile.StoppedByRFID = true;
		else
			chargeSessionFromFile.StoppedByRFID = false;

		strncpy(chargeSessionFromFile.AuthenticationCode ,cJSON_GetObjectItem(jsonSession,"AuthenticationCode")->valuestring, 41);


		ESP_LOGI(TAG, "SessionId=%s",chargeSessionFromFile.SessionId);
		ESP_LOGI(TAG, "Energy=%f",chargeSessionFromFile.Energy);
		ESP_LOGI(TAG, "StartDateTime=%s",chargeSessionFromFile.StartDateTime);
		ESP_LOGI(TAG, "EndDateTime=%s",chargeSessionFromFile.EndDateTime);
		ESP_LOGI(TAG, "ReliableClock=%d",chargeSessionFromFile.ReliableClock);
		ESP_LOGI(TAG, "StoppedByRFID=%d",chargeSessionFromFile.StoppedByRFID);
		ESP_LOGI(TAG, "AuthenticationCode=%s",chargeSessionFromFile.AuthenticationCode);

	}

	if(jsonSession != NULL)
		cJSON_Delete(jsonSession);


	/// Go to length index
	fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

	uint32_t nrOfOCMFElements = 0;
	fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
	ESP_LOGI(TAG, "NrOfElements read: %i", nrOfOCMFElements);
	/// Build OCMF strings for each element
	if(nrOfOCMFElements > 0)
	{
		int i;
		for (i = 0; i < nrOfOCMFElements; i++)
		{
			/// Go to element position
			int newElementPosition = (FILE_OCMF_START_ADDR_1004) + ((i) * sizeof(struct LogOCMFData));
			fseek(sessionFile, newElementPosition, SEEK_SET);

			struct LogOCMFData OCMFElement;
			fread(&OCMFElement, sizeof(struct LogOCMFData), 1, sessionFile);

			/// Hold'n clear crc to get correct calculation for packet
			uint32_t packetCrc = OCMFElement.crc;
			OCMFElement.crc = 0;

			uint32_t crcCalc = crc32_normal(0, &OCMFElement, sizeof(struct LogOCMFData));

			ESP_LOGW(TAG, "OCMF read %i addr: %i : %c %i %f 0x%X %s", i, newElementPosition, OCMFElement.label, OCMFElement.timestamp, OCMFElement.energy, packetCrc, (crcCalc == packetCrc) ? "MATCH" : "FAIL");
		}

	}

	fclose(sessionFile);

	free(base64SessionData);
	free(sessionDataCreated);

	return ESP_OK;
}

esp_err_t offlineSession_ReadChargeSessionFromFile(int fileNo, cJSON * jsonSession)
{
	char buf[22] = {0};
	sprintf(buf,"/offs/%d.bin", fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		return ESP_FAIL;
	}

	uint8_t fileVersion = 0;
	fread(&fileVersion, 1, 1, sessionFile);
	ESP_LOGW(TAG, "File version: %d", fileVersion);

	/// Read session CRC
	fseek(sessionFile, FILE_SESSION_CRC_ADDR_996, SEEK_SET);
	uint32_t crcRead = 0;
	fread(&crcRead, sizeof(uint32_t), 1, sessionFile);

	/// Go to beginning before reading
	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);


	char * base64SessionData = calloc(1000-4, 1);
	volatile size_t size = fread(base64SessionData, 1000-4, 1, sessionFile);

	/// Find first \0 element in the array, index = length of base64 encoded CompletedSession-structure
	/*int endIndex = 0;
	for (endIndex = 0; endIndex < 996; endIndex++)
	{
		if(base64SessionData[endIndex] == '\0')
		{
			break;
		}
	}*/

	//int base64SessionDataLen = endIndex + 1;
	int base64SessionDataLen = strlen(base64SessionData);

	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);

	ESP_LOGW(TAG,"Read session: %i, %i:, %s", base64SessionDataLen, strlen(base64SessionData), base64SessionData);

	ESP_LOGW(TAG,"Session CRC read control: 0x%X vs 0x%X: %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");

	if(crcRead != crcCalc)
	{
		return ESP_ERR_INVALID_CRC;
	}



	size_t outLen = 0;

	char *sessionDataCreated = (char*)base64_decode(base64SessionData, base64SessionDataLen, &outLen);

	printf("%d: %s\n", strlen(base64SessionData), base64SessionData);
	printf("%d: %.*s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

	struct ChargeSession chargeSessionFromFile = {0};

	jsonSession = cJSON_Parse(sessionDataCreated);

	if(cJSON_HasObjectItem(jsonSession, "SessionId") &&
			cJSON_HasObjectItem(jsonSession, "Energy") &&
			cJSON_HasObjectItem(jsonSession, "StartDateTime") &&
			cJSON_HasObjectItem(jsonSession, "EndDateTime") &&
			cJSON_HasObjectItem(jsonSession, "ReliableClock") &&
			cJSON_HasObjectItem(jsonSession, "StoppedByRFID")&&
			cJSON_HasObjectItem(jsonSession, "AuthenticationCode"))
	{
		strncpy(chargeSessionFromFile.SessionId, 	cJSON_GetObjectItem(jsonSession,"SessionId")->valuestring, 37);
		chargeSessionFromFile.Energy = 				cJSON_GetObjectItem(jsonSession,"Energy")->valuedouble;
		strncpy(chargeSessionFromFile.StartDateTime,	cJSON_GetObjectItem(jsonSession,"StartDateTime")->valuestring, 32);
		strncpy(chargeSessionFromFile.EndDateTime,	cJSON_GetObjectItem(jsonSession,"EndDateTime")->valuestring, 32);

		if(cJSON_GetObjectItem(jsonSession,"ReliableClock")->valueint > 0)
			chargeSessionFromFile.ReliableClock = true;
		else
			chargeSessionFromFile.ReliableClock = false;

		if(cJSON_GetObjectItem(jsonSession,"StoppedByRFID")->valueint > 0)
			chargeSessionFromFile.StoppedByRFID = true;
		else
			chargeSessionFromFile.StoppedByRFID = false;

		strncpy(chargeSessionFromFile.AuthenticationCode ,cJSON_GetObjectItem(jsonSession,"AuthenticationCode")->valuestring, 41);


		ESP_LOGI(TAG, "SessionId=%s",chargeSessionFromFile.SessionId);
		ESP_LOGI(TAG, "Energy=%f",chargeSessionFromFile.Energy);
		ESP_LOGI(TAG, "StartDateTime=%s",chargeSessionFromFile.StartDateTime);
		ESP_LOGI(TAG, "EndDateTime=%s",chargeSessionFromFile.EndDateTime);
		ESP_LOGI(TAG, "ReliableClock=%d",chargeSessionFromFile.ReliableClock);
		ESP_LOGI(TAG, "StoppedByRFID=%d",chargeSessionFromFile.StoppedByRFID);
		ESP_LOGI(TAG, "AuthenticationCode=%s",chargeSessionFromFile.AuthenticationCode);

	}

	if(jsonSession != NULL)
		cJSON_Delete(jsonSession);


	/// Go to length index
	fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

	uint32_t nrOfOCMFElements = 0;
	fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
	ESP_LOGI(TAG, "NrOfElements read: %i", nrOfOCMFElements);
	/// Build OCMF strings for each element
	if(nrOfOCMFElements > 0)
	{
		int i;
		for (i = 0; i < nrOfOCMFElements; i++)
		{
			/// Go to element position
			int newElementPosition = (FILE_OCMF_START_ADDR_1004) + ((i) * sizeof(struct LogOCMFData));
			fseek(sessionFile, newElementPosition, SEEK_SET);

			struct LogOCMFData OCMFElement;
			fread(&OCMFElement, sizeof(struct LogOCMFData), 1, sessionFile);

			/// Hold'n clear crc to get correct calculation for packet
			uint32_t packetCrc = OCMFElement.crc;
			OCMFElement.crc = 0;

			uint32_t crcCalc = crc32_normal(0, &OCMFElement, sizeof(struct LogOCMFData));

			ESP_LOGW(TAG, "OCMF read %i addr: %i : %c %i %f 0x%X %s", i, newElementPosition, OCMFElement.label, OCMFElement.timestamp, OCMFElement.energy, packetCrc, (crcCalc == packetCrc) ? "MATCH" : "FAIL");
		}

	}

	fclose(sessionFile);

	free(base64SessionData);
	free(sessionDataCreated);

	return ESP_OK;
}





esp_err_t offlineSession_GetSignedSessionFromActiveFile(cJSON* entryArray)
{
	char buf[22] = {0};
	sprintf(buf,"/offs/%d.bin", 0);
	sessionFile = fopen(buf, "r");
	//sessionFile = fopen(activePathString, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		return ESP_FAIL;
	}

	uint8_t fileVersion = 0;
	fread(&fileVersion, 1, 1, sessionFile);
	ESP_LOGW(TAG, "File version: %d", fileVersion);

	/*
	/// Go to beginning before reading
	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);



	char * base64SessionData = calloc(1000, 1);
	volatile size_t size = fread(base64SessionData, 1000, 1, sessionFile);

	/// Find first \0 element in the array, index = length of base64 encoded CompletedSession-structure
	int endIndex = 0;
	for (endIndex = 0; endIndex <= 999; endIndex++)
	{
		if(base64SessionData[endIndex] == '\0')
		{
			break;
		}
	}


	int base64SessionDataLen = endIndex;
	size_t outLen = 0;

	char *sessionDataCreated = (char*)base64_decode(base64SessionData, base64SessionDataLen, &outLen);

	printf("%d: %s\n", strlen(base64SessionData), base64SessionData);
	printf("%d: %.*s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

	struct ChargeSession chargeSessionFromFile = {0};

	cJSON *jsonSession = cJSON_Parse(sessionDataCreated);

	if(cJSON_HasObjectItem(jsonSession, "SessionId") &&
			cJSON_HasObjectItem(jsonSession, "Energy") &&
			cJSON_HasObjectItem(jsonSession, "StartDateTime") &&
			cJSON_HasObjectItem(jsonSession, "EndDateTime") &&
			cJSON_HasObjectItem(jsonSession, "ReliableClock") &&
			cJSON_HasObjectItem(jsonSession, "StoppedByRFID")&&
			cJSON_HasObjectItem(jsonSession, "AuthenticationCode"))
	{
		strncpy(chargeSessionFromFile.SessionId, 	cJSON_GetObjectItem(jsonSession,"SessionId")->valuestring, 37);
		chargeSessionFromFile.Energy = 				cJSON_GetObjectItem(jsonSession,"Energy")->valuedouble;
		strncpy(chargeSessionFromFile.StartDateTime,	cJSON_GetObjectItem(jsonSession,"StartDateTime")->valuestring, 32);
		strncpy(chargeSessionFromFile.EndDateTime,	cJSON_GetObjectItem(jsonSession,"EndDateTime")->valuestring, 32);

		if(cJSON_GetObjectItem(jsonSession,"ReliableClock")->valueint > 0)
			chargeSessionFromFile.ReliableClock = true;
		else
			chargeSessionFromFile.ReliableClock = false;

		if(cJSON_GetObjectItem(jsonSession,"StoppedByRFID")->valueint > 0)
			chargeSessionFromFile.StoppedByRFID = true;
		else
			chargeSessionFromFile.StoppedByRFID = false;

		strncpy(chargeSessionFromFile.AuthenticationCode ,cJSON_GetObjectItem(jsonSession,"AuthenticationCode")->valuestring, 41);


		ESP_LOGI(TAG, "SessionId=%s",chargeSessionFromFile.SessionId);
		ESP_LOGI(TAG, "Energy=%f",chargeSessionFromFile.Energy);
		ESP_LOGI(TAG, "StartDateTime=%s",chargeSessionFromFile.StartDateTime);
		ESP_LOGI(TAG, "EndDateTime=%s",chargeSessionFromFile.EndDateTime);
		ESP_LOGI(TAG, "ReliableClock=%d",chargeSessionFromFile.ReliableClock);
		ESP_LOGI(TAG, "StoppedByRFID=%d",chargeSessionFromFile.StoppedByRFID);
		ESP_LOGI(TAG, "AuthenticationCode=%s",chargeSessionFromFile.AuthenticationCode);

	}

	if(jsonSession != NULL)
			cJSON_Delete(jsonSession);
*/

	/// Go to length index
	fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

	uint32_t nrOfOCMFElements = 0;
	fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
	ESP_LOGI(TAG, "NrOfElements read: %i", nrOfOCMFElements);
	/// Build OCMF strings for each element
	if(nrOfOCMFElements > 0)
	{
		int i;
		for (i = 0; i < nrOfOCMFElements; i++)
		{
			/// Go to element position
			int newElementPosition = (FILE_OCMF_START_ADDR_1004) + ((i) * sizeof(struct LogOCMFData));
			fseek(sessionFile, newElementPosition, SEEK_SET);


				struct LogOCMFData OCMFElement;
				fread(&OCMFElement, sizeof(struct LogOCMFData), 1, sessionFile);

				/// Hold'n clear crc got get correct calculation for packet
				uint32_t packetCrc = OCMFElement.crc;
				OCMFElement.crc = 0;

				uint32_t crcCalc = crc32_normal(0, &OCMFElement, sizeof(struct LogOCMFData));

				ESP_LOGW(TAG, "OCMF read %i addr: %i : %c %i %f 0x%X %s", i, newElementPosition, OCMFElement.label, OCMFElement.timestamp, OCMFElement.energy, packetCrc, (crcCalc == packetCrc) ? "MATCH" : "FAIL");

				if((crcCalc == packetCrc))
				{
					cJSON * logArrayElement = cJSON_CreateObject();
					char timeBuffer[50] = {0};
					zntp_format_time(timeBuffer, OCMFElement.timestamp);
					//zntp_GetSystemTime(timeBuffer, NULL);
					char* tx = &OCMFElement.label;

					cJSON_AddStringToObject(logArrayElement, "TM", timeBuffer);	//TimeAndSyncState
					cJSON_AddStringToObject(logArrayElement, "TX", tx);	//Message status (B, T, E)
					cJSON_AddNumberToObject(logArrayElement, "RV", OCMFElement.energy);//get_accumulated_energy());	//ReadingValue
					cJSON_AddStringToObject(logArrayElement, "RI", "1-0:1.8.0");	//ReadingIdentification(OBIS-code)
					cJSON_AddStringToObject(logArrayElement, "RU", "kWh");			//ReadingUnit
					cJSON_AddStringToObject(logArrayElement, "RT", "AC");			//ReadingCurrentType
					cJSON_AddStringToObject(logArrayElement, "ST", "G");			//MeterState

					cJSON_AddItemToArray(entryArray, logArrayElement);
				}
		}
	}

	fclose(sessionFile);

	//free(base64SessionData);
	//free(sessionDataCreated);

	return ESP_OK;
}



//static cJSON * fileRoot = NULL;
/*static cJSON * fileReaderArray = NULL;
cJSON * OCMF_AddFileElementToSession_no_lock(const char * const tx, const char * const st, time_t time_in, double energy_in)
{

	if(fileReaderArray != NULL)
	{
		int arrayLength = cJSON_GetArraySize(fileReaderArray);

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
			cJSON_AddStringToObject(logArrayElement, "ST", st);			//MeterState

			cJSON_AddItemToArray(fileReaderArray, logArrayElement);

			ESP_LOGW(TAG, "OCMF Array size: %i: ", cJSON_GetArraySize(fileReaderArray));
		}
		else
		{
			ESP_LOGW(TAG, "MAX OCMF Array size reached");
		}
	}
	return fileReaderArray;
}*/







esp_err_t offlineSession_SaveSession(char * sessionData)
{
	int ret = 0;

	if(!offlineSession_mount_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}


	/// Search for file number to use for this session
	activeFileNumber = offlineSession_FindLatestFile();
	if(activeFileNumber < 0)
	{
		return ESP_FAIL;
	}


	sprintf(activePathString,"/offs/%d.bin", activeFileNumber);

	sessionFile = fopen(activePathString, "wb+");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Save: sessionFile == NULL");
		return ESP_FAIL;
	}


	//Save the session structure to the file including the start 'B' message
	offlineSession_UpdateSessionOnFile(sessionData);

	fclose(sessionFile);



	//offlineSession_ReadFileContent();

	//offlineSession_append_energy('B', 123, 0.1);

	//offlineSession_append_energy('T', 1234, 0.2);

	//offlineSession_append_energy('E', 12345, 0.3);


	//int oldest = offlineSession_FindOldestFile();

	//offlineSession_ReadFileContent

	return ret;
}


void offlineSession_append_energy(char label, int timestamp, double energy)
{
	sessionFile = fopen(activePathString, "rb+");
	uint32_t nrOfOCMFElements = 0;
	if(sessionFile != NULL)
	{
		/// Find end of file to get size
		fseek(sessionFile, 0L, SEEK_END);
		size_t readSize = ftell(sessionFile);
		ESP_LOGW(TAG, "FileNo %d: %c, filesize: %d", activeFileNumber, label, readSize);

		fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

		/// Nr of elements is known for B, but not for T and E.
		if(label == 'B')
		{
			/// Set first element
			nrOfOCMFElements = 0;
			offlineSessionOpen = true;
		}
		else
		{
			/// Make sure no attempts to write energy occurs until the Begin label is set
			if(offlineSessionOpen == false)
			{
				ESP_LOGE(TAG, "Tried appending energy with unopened offlinesession: %c", label);
				return;
			}

			/// Make sure no attempts to write energy occurs after the End label is set
			if(label == 'E')
				offlineSessionOpen = false;


			/// Get nr of elements...
			fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);

			if((nrOfOCMFElements == 0) || (nrOfOCMFElements >= 99))
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %d", activeFileNumber, nrOfOCMFElements);
				return;
			}
		}

		ESP_LOGW(TAG, "FileNo %d: &1000: Nr of OCMF elements: %c: %d", activeFileNumber, label, nrOfOCMFElements);

		/// And add 1.

		/// Prepare struct with crc
		struct LogOCMFData line = {.label = label, .energy = energy, .timestamp = timestamp, .crc = 0};
		uint32_t crc = crc32_normal(0, &line, sizeof(struct LogOCMFData));
		line.crc = crc;

		//ESP_LOGW(TAG, "FileNo %d: writing to OFFS-file with crc=%u", activeFileNumber, line.crc);

		/// Find new element position
		int elementOffset = 0;
		//if(nrOfOCMFElements > 0)
			//elementOffset = nrOfOCMFElements - 1;

		int newElementPosition = (FILE_OCMF_START_ADDR_1004) + (nrOfOCMFElements * sizeof(struct LogOCMFData));
		ESP_LOGW(TAG, "FileNo %d: New element position: #%d: %d", activeFileNumber, nrOfOCMFElements, newElementPosition);
		ESP_LOGW(TAG, "OCMF Write %i addr: %i : %c %i %f 0x%X", nrOfOCMFElements, newElementPosition, line.label, line.timestamp, line.energy, line.crc);

		/// Write new element
		fseek(sessionFile, newElementPosition, SEEK_SET);
		fwrite(&line, sizeof(struct LogOCMFData), 1, sessionFile);

		/// Update nr of elements @1000
		fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);
		nrOfOCMFElements += 1;
		fwrite(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);

		fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

		nrOfOCMFElements = 99;
		fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
		ESP_LOGW(TAG, "FileNo %d: Nr elements: #%d", activeFileNumber, nrOfOCMFElements);
		fclose(sessionFile);
	}
}

int offlineSession_ReadOldestSession(char * SessionString)
{

	sessionFile = fopen(activePathString, "rb+");
	uint32_t nrOfOCMFElements = 0;
	if(sessionFile != NULL)
	{
		/// Find end of file to get size
		fseek(sessionFile, 0L, SEEK_END);
		size_t readSize = ftell(sessionFile);
		ESP_LOGW(TAG, "FileNo %d: filesize: %d", activeFileNumber, readSize);

		fseek(sessionFile, 1000, SEEK_SET);
	}

	return 0;
}

//int offlineSession_attempt_send(void){
//    ESP_LOGI(TAG, "log data:");
//
//    int result = -1;
//
//    int log_start;
//    int log_end;
//    FILE *fp = init_log(&log_start, &log_end);
//
//    //If file or partition is now available, indicate empty file
//    if(fp == NULL)
//    	return 0;
//
//    char ocmf_text[200] = {0};
//
//    while(log_start!=log_end){
//        struct LogLine line;
//        int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_start);
//        fseek(fp, start_of_line, SEEK_SET);
//        int read_result = fread(&line, 1,sizeof(line),  fp);
//
//        uint32_t crc_on_file = line.crc;
//        line.crc = 0;
//        uint32_t calculated_crc = crc32_normal(0, &line, sizeof(line));
//
//
//        ESP_LOGI(TAG, "LogLine@%d>%d: E=%f, t=%d, crc=%d, valid=%d, read=%d",
//            log_start, start_of_line,
//            line.energy, line.timestamp,
//            crc_on_file, crc_on_file==calculated_crc, read_result
//        );
//
//        if(crc_on_file==calculated_crc){
//            OCMF_CreateMessageFromLog(ocmf_text, line.timestamp, line.energy);
//            int publish_result = publish_string_observation_blocked(
//			    SignedMeterValue, ocmf_text, 2000
//		    );
//
//            if(publish_result<0){
//                ESP_LOGI(TAG, "publishing line failed, aborting log dump");
//                break;
//            }
//
//            int new_log_start = (log_start + 1) % max_offline_session_files;
//            update_header(fp, new_log_start, log_end);
//            fflush(fp);
//
//
//            ESP_LOGI(TAG, "line published");
//
//
//        }else{
//            ESP_LOGI(TAG, "skipped corrupt line");
//        }
//
//        log_start = (log_start+1) % max_offline_session_files;
//    }
//
//    if(log_start==log_end){
//        result = 0;
//        if(log_start!=0)
//            update_header(fp, 0, 0);
//    }
//
//	int close_result = fclose(fp);
//    ESP_LOGI(TAG, "closed log file %d", close_result);
//    return result;
//}


int offlineSession_delete_session(int fileNo)
{
	int ret = 0;

	if(!offlineSession_mount_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	char buf[22] = {0};
	sprintf(buf,"/offs/%d.bin", fileNo);

	FILE *fp = fopen(buf, "r");
	if(fp==NULL)
	{
		ESP_LOGE(TAG, "%d: Before remove: logfile can't be opened ", fileNo);
		return 0;
	}
	else
		ESP_LOGE(TAG, "%d: Before remove: logfile can be opened ", fileNo);

	fclose(fp);

	remove(buf);

	fp = fopen(buf, "r");
	if(fp==NULL)
	{
		ESP_LOGE(TAG, "%d: After remove: logfile deleted SUCCEEDED", fileNo);
		ret = 1;
	}
	else
	{
		ESP_LOGE(TAG, "%d: After remove: logfile delete FAILED ", fileNo);
		ret = 2;
	}

	fclose(fp);

	return ret;
}
