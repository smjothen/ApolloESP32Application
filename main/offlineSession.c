#include "offlineSession.h"

#define TAG "OFFLINE_SESSION"

#include <limits.h>

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
#include "../components/i2c/include/i2cDevices.h"

#include "../components/ocpp/include/messages/call_messages/ocpp_call_request.h"
#include "../components/ocpp/include/types/ocpp_reason.h"
#include "../components/ocpp/include/types/ocpp_meter_value.h"

static char * tmp_path = "/files";

static const int max_offline_session_files = 100;
static const int max_offline_signed_values = 100;

static const int max_offline_ocpp_txn_files = 100;

#define FILE_VERSION_ADDR_0  		0L
#define FILE_SESSION_ADDR_2  		2L
#define FILE_SESSION_CRC_ADDR_996	996L
#define FILE_NR_OF_OCMF_ADDR_1000  	1000L
#define FILE_OCMF_START_ADDR_1004 	1004L

#define FILE_TXN_ADDR_ID 1L
#define FILE_TXN_ADDR_START 5L
#define FILE_TXN_ADDR_STOP 66L
#define FILE_TXN_ADDR_METER 139L

#define OCPP_MIN_TIMESTAMP 1650000000
#define OCPP_MAX_FILE_SIZE 2000

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

static SemaphoreHandle_t offs_lock;
static TickType_t lock_timeout = pdMS_TO_TICKS(1000*5);

static bool offlineSessionOpen = false;

static int activeFileNumber = -1;
static char activePathString[22] = {0};
static char readingPath_ocpp[20] = {0};
static long readingOffset_ocpp = LONG_MAX;
static FILE *sessionFile = NULL;
static int maxOfflineSessionsCount = 0;

bool offlineSession_select_folder()
{
	struct stat st;
	if(stat("/files", &st) == 0){
		if(tmp_path[1] != 'f')
			sprintf(tmp_path, "/files");

		ESP_LOGI(TAG, "'%s' already mounted", tmp_path);
		return true;
	}

	//If failing to mount, try using partition for chargers numbers below ~ZAP000150
	if((errno == ENOENT && errno == ENODEV) && (i2cCheckSerialForDiskPartition() == true)){
		if(stat("/disk", &st) == 0){
			if(tmp_path[1] != 'd')
				sprintf(tmp_path, "/disk");

			ESP_LOGI(TAG, "'%s' already mounted", tmp_path);
			return true;
		}
	}

	return false;
}

void offlineSession_Init()
{
	offs_lock = xSemaphoreCreateMutex();
	xSemaphoreGive(offs_lock);

	offlineSession_select_folder();
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
int offlineSession_FindNewFileNumber()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	int fileNo = 0;
	FILE *file;
	char buf[22] = {0};

	ESP_LOGW(TAG, "Searching for first unused OfflineSession file...");

	//for (fileNo = 0; fileNo < max_offline_session_files; fileNo++)
	for (fileNo = max_offline_session_files-1; fileNo >= 0; fileNo--)
	{
		sprintf(buf,"%s/%d.bin", tmp_path, fileNo);

		file = fopen(buf, "r");
		if(file != NULL)
		{
			fclose(file);

			//Max file (99) is used, no available
			if(fileNo == (max_offline_session_files - 1))
				return -1;
			else
			{
				fileNo = fileNo + 1;
				break; //Found unused file, abort search
			}
		}
		else
		{
			///Found 0 as first unused file
			if(fileNo == 0)
				break;
		}
	}

	ESP_LOGW(TAG, "Found from top as first unused OfflineSession file: %d", fileNo);

	return fileNo;
}

/*
 * Find the oldest file to read, send, delete
 */
int offlineSession_FindOldestFile()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	int fileNo = 0;
	FILE *file;
	char buf[32] = {0};

	for (fileNo = 0; fileNo < max_offline_session_files; fileNo++ )
	{
		sprintf(buf,"%s/%d.bin", tmp_path, fileNo);

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

long offlineSession_FindOldestFile_ocpp()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return LONG_MAX;

	ESP_LOGI(TAG, "Attempting to find oldest file (ocpp)");

	DIR * dir = opendir(tmp_path);
	if(dir == NULL){
		ESP_LOGE(TAG, "Unable to open directory (%s) to find oledst file", tmp_path);
		return -1;
	}

	long oldest = LONG_MAX;
	struct dirent * dp = readdir(dir);
	while(dp != NULL){
		if(dp->d_type == DT_REG){
			long timestamp = strtol(dp->d_name, NULL, 16);

			if(timestamp > OCPP_MIN_TIMESTAMP && timestamp != LONG_MAX){
				if(timestamp < oldest)
					oldest = timestamp;
			}
		}
		dp = readdir(dir);
	}
	closedir(dir);

	if(oldest != LONG_MAX){
		ESP_LOGI(TAG, "Oldest ocpp offline session is dated %ld", oldest);
	}else{
		ESP_LOGI(TAG, "No ocpp offline session found");
	}

	return oldest;
}

int offlineSession_FindNrOfFiles()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	int fileNo = 0;
	FILE *file;
	char buf[32] = {0};
	int fileCount = 0;

	for (fileNo = 0; fileNo < max_offline_session_files; fileNo++ )
	{
		sprintf(buf,"%s/%d.bin", tmp_path, fileNo);

		file = fopen(buf, "r");
		if(file != NULL)
		{
			fclose(file);
			fileCount++;
			ESP_LOGW(TAG, "OfflineSession found file: %d", fileNo);
		}

	}
	ESP_LOGW(TAG, "Nr of OfflineSession files: %d", fileCount);

	if(fileCount > maxOfflineSessionsCount)
		maxOfflineSessionsCount = fileCount;

	return fileCount;
}

int offlineSession_FindNrOfFiles_ocpp()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	ESP_LOGI(TAG, "Attemting to find nr of files (ocpp)");
	int fileCount = 0;

	DIR * dir = opendir(tmp_path);
	if(dir == NULL){
		return 0;
	}

	struct dirent * dp = readdir(dir);
	while(dp != NULL){
		if(dp->d_type == DT_REG){
			long timestamp = strtol(dp->d_name, NULL, 16);

			if(timestamp > OCPP_MIN_TIMESTAMP && timestamp != LONG_MAX){
				ESP_LOGI(TAG, "OfflineSession found file: %s", dp->d_name);
				fileCount++;
			}
		}
		dp = readdir(dir);
	}

	ESP_LOGI(TAG, "Nr of OfflineSession files: %d (ocpp)", fileCount);

	closedir(dir);
	return fileCount;
}


int offlineSession_GetMaxSessionCount()
{
	int tmp = maxOfflineSessionsCount;
	maxOfflineSessionsCount = 0;
	return tmp;
}

int offlineSession_CheckIfLastLessionIncomplete(struct ChargeSession *incompleteSession)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	int fileNo = 0;
	FILE *lastUsedFile;
	char buf[22] = {0};

	/// First check file 0 to see if there are any offlineSession files
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);
	lastUsedFile = fopen(buf, "r");
	if(lastUsedFile == NULL)
	{
		/// No file nr 0 found
		ESP_LOGI(TAG, "No offline files found during boot with car connected");
		return -1;
	}
	else
	{
		/// File nr 0 found
		fclose(lastUsedFile);
	}

	/// Then perform search from top to see if there are later files
	for (fileNo = max_offline_session_files-1; fileNo >= 0; fileNo-- )
	{
		sprintf(buf,"%s/%d.bin", tmp_path, fileNo);

		lastUsedFile = fopen(buf, "r");
		if(lastUsedFile != NULL)
		{
			cJSON * lastSession = cJSON_CreateObject();
			lastSession = offlineSession_ReadChargeSessionFromFile(fileNo);

			if(cJSON_HasObjectItem(lastSession, "EndDateTime"))
			{
				int i = strlen(cJSON_GetObjectItem(lastSession,"EndDateTime")->valuestring);
				if (i > 0)
				{
					ESP_LOGE(TAG, "EndDateTime has length %d. Session is COMPLETE", i);
					activeFileNumber = -1;
				}
				else
				{
					ESP_LOGE(TAG, "EndDateTime has length %d. Session is IN-COMPLETE", i);

					/// Read the incomplete structure from file
					if(cJSON_HasObjectItem(lastSession, "SessionId") &&
							cJSON_HasObjectItem(lastSession, "Energy") &&
							cJSON_HasObjectItem(lastSession, "StartDateTime") &&
							cJSON_HasObjectItem(lastSession, "ReliableClock") &&
							cJSON_HasObjectItem(lastSession, "StoppedByRFID")&&
							cJSON_HasObjectItem(lastSession, "AuthenticationCode"))
					{
						strncpy(incompleteSession->SessionId, 	cJSON_GetObjectItem(lastSession,"SessionId")->valuestring, 37);
						incompleteSession->Energy = 				cJSON_GetObjectItem(lastSession,"Energy")->valuedouble;
						strncpy(incompleteSession->StartDateTime,	cJSON_GetObjectItem(lastSession,"StartDateTime")->valuestring, 32);
						strncpy(incompleteSession->EndDateTime,	cJSON_GetObjectItem(lastSession,"EndDateTime")->valuestring, 32);

						if(cJSON_GetObjectItem(lastSession,"ReliableClock")->valueint > 0)
							incompleteSession->ReliableClock = true;
						else
							incompleteSession->ReliableClock = false;

						if(cJSON_GetObjectItem(lastSession,"StoppedByRFID")->valueint > 0)
							incompleteSession->StoppedByRFID = true;
						else
							incompleteSession->StoppedByRFID = false;

						strncpy(incompleteSession->AuthenticationCode ,cJSON_GetObjectItem(lastSession,"AuthenticationCode")->valuestring, 41);

						ESP_LOGI(TAG, "SessionId=%s",incompleteSession->SessionId);
						ESP_LOGI(TAG, "Energy=%f",incompleteSession->Energy);
						ESP_LOGI(TAG, "StartDateTime=%s",incompleteSession->StartDateTime);
						ESP_LOGI(TAG, "EndDateTime=%s",incompleteSession->EndDateTime);
						ESP_LOGI(TAG, "ReliableClock=%d",incompleteSession->ReliableClock);
						ESP_LOGI(TAG, "StoppedByRFID=%d",incompleteSession->StoppedByRFID);
						ESP_LOGI(TAG, "AuthenticationCode=%s",incompleteSession->AuthenticationCode);
					}

					/// Must set activeFileNumber, path and offlineSessionOpen to allow new SignedValues entries
					activeFileNumber = fileNo;
					sprintf(activePathString,"%s/%d.bin", tmp_path, activeFileNumber);
					offlineSessionOpen = true;
				}
			}

			cJSON_Delete(lastSession);

			/// Found the last file from top
			fclose(lastUsedFile);

			ESP_LOGW(TAG, "OfflineSession found last used file from top: %d", fileNo);

			/// End loop with fileNo >= 0 if last session was Incomplete
			break;
		}
	}

	return activeFileNumber;
}


/// Call this function to ensure that the sessionFile is no longer accessed
static int lastUsedFileNumber = -1;
void offlineSession_SetSessionFileInactive()
{
	lastUsedFileNumber = activeFileNumber;
	activeFileNumber = -1;
	strcpy(activePathString, "");
}

void offlineSession_DeleteLastUsedFile()
{
	if((activeFileNumber == -1) && (lastUsedFileNumber >= 0))
	{
		ESP_LOGE(TAG, "### Deleting last session because LOCAL only with energy = 0.0 ###");
		offlineSession_delete_session(lastUsedFileNumber);
		lastUsedFileNumber = -1;
	}
}

void offlineSession_UpdateSessionOnFile(char *sessionData, bool createNewFile)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return;

	if(activeFileNumber < 0)
		return;


	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return;
	}

	if(createNewFile)
		sessionFile = fopen(activePathString, "wb+");
	else
		sessionFile = fopen(activePathString, "rb+");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Could not create or open sessionFile");
		xSemaphoreGive(offs_lock);
		return;
	}

	//ESP_LOGW(TAG, "strlen: %d, path: %s", strlen(sessionData), activePathString);

	uint8_t fileVersion = 1;
	fseek(sessionFile, FILE_VERSION_ADDR_0, SEEK_SET);
	fwrite(&fileVersion, 1, 1, sessionFile);

	int sessionDataLen = strlen(sessionData);
	size_t outLen = 0;
	char * base64SessionData = base64_encode(sessionData, sessionDataLen, &outLen);
	volatile int base64SessionDataLen = strlen(base64SessionData);

	ESP_LOGW(TAG,"%d: %s\n", strlen(sessionData), sessionData);
	//ESP_LOGW(TAG,"%d: %s\n", strlen(base64SessionData), base64SessionData);

	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);
	fwrite(base64SessionData, base64SessionDataLen, 1, sessionFile);

	///Write CRC at the end of the block
	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);
	fseek(sessionFile, FILE_SESSION_CRC_ADDR_996, SEEK_SET);
	fwrite(&crcCalc, sizeof(uint32_t), 1, sessionFile);

	//ESP_LOGW(TAG, "Session CRC:: 0x%X", crcCalc);

	free(base64SessionData);
	fclose(sessionFile);

	xSemaphoreGive(offs_lock);
}

static esp_err_t writeSessionData_ocpp(FILE * fp, const unsigned char * sessionData, size_t data_length){
	int sessionDataLen = data_length;
	size_t outlen = 0;
	char * base64SessionData = base64_encode(sessionData, sessionDataLen, &outlen);

	time_t write_time = time(NULL);
	if(fwrite(&write_time, sizeof(time_t), 1, fp) != 1)
		goto error;

	if(fwrite(&outlen, sizeof(size_t), 1, fp) != 1)
		goto error;

	if(fwrite(base64SessionData, sizeof(char), outlen, fp) != outlen)
		goto error;


	///Write CRC at the end of the block
	uint32_t crcCalc = crc32_normal(0, base64SessionData, outlen);
	if(fwrite(&crcCalc, sizeof(uint32_t), 1, fp) != 1)
		goto error;

	free(base64SessionData);
	base64SessionData = NULL;

	return ESP_OK;
error:
	ESP_LOGE(TAG, "Unable to write session data");
	free(base64SessionData);
	return ESP_FAIL;
}

bool peek_tainted = true;
time_t peeked_timestamp = LONG_MAX;

/*
 * The ocpp file structure for version 1:
 *
 * <version> at: FILE_VERSION_ADDR_0
 * <transaction_id> at: FILE_TXN_ADDR_ID
 * <StartTransaction.req> at: FILE_TXN_ADDR_START
 * <StopTransaction.req> at: FILE_TXN_ADDR_STOP
 * <MeterValue.req><...><stop_txn_data> FILE_TXN_ADDR_METER
 *
 * <version>: uint8_t
 * <transaction_id>: int
 * <StartTransaction.req>: <sessionData>
 * <StopTransaction.req>: <sessionData>
 * <MeterValue.req>: <sessionData>
 * <stop_txn_data>: <sessionData>
 *
 * <sessionData>: <timestamp><data_length><data><crc>
 * <timestamp>: <time_t>
 * <data_length>: <size_t>
 * <data>: <unsigned char><...>
 * <crc>: <uint32_t>
 *
 * NOTE: All sections of type <sessionData> may not be written to file depending on when the ocpp went offline/online.
 * startTransaction might have already been sendt or enqueued, transaction_id might be a temporary value if it has
 * not been recievedfrom CS. If stop_txn_data exists then StopTransaction.req should also exist.
 *
 * new lines in the format above does not indicate '\n' and '' in the file and is only there to make
 * documentation easier to read and show offset.
 *
 * Meter values are not stored with connector id. All connector ids written (also for start transaction)
 * should be 1, as all values should relate to a transaction on the only connector present on the charger.
 *
 * File names are 8.3 filenames, containing a directory prefix followed by an (up to) 8 character name followed by '.' and
 * 3 character long extention. The file name is given by the timestamp of the start transaction. Tue to the filename limit
 * of 8 characters, we write it as 8 width hexadecimal. It will overflow on Sun Feb 07 2106 06:28:15
 */
static esp_err_t offlineSession_UpdateSessionOnFile_Ocpp(const unsigned char * sessionData, size_t data_length, int transaction_id, time_t start_transaction_timestamp, long offset, int whence, bool append)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0 || start_transaction_timestamp < OCPP_MIN_TIMESTAMP)
		return ESP_FAIL;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "Failed to obtain offs lock during ocpp update");
		return ESP_ERR_TIMEOUT; // EAGAIN / EWOULDBLOCK / EDEADLK ?
	}

	FILE * fp = NULL;
	bool is_init = false;

	errno = 0;
	char file_path[20];
	int written_length = snprintf(file_path, sizeof(file_path), "%s/%.8lx.bin", tmp_path, start_transaction_timestamp);
	if(written_length < 0 || written_length >= sizeof(file_path)){
		ESP_LOGE(TAG, "Unable to write active path");
		goto error;
	}

	if(access(file_path, F_OK) != 0){
		if(offlineSession_FindNrOfFiles_ocpp() < max_offline_ocpp_txn_files){
			ESP_LOGI(TAG, "Creating session file: %s  timestamp: %ld (ocpp)", file_path, start_transaction_timestamp);
			fp = fopen(file_path, "wb+");
			is_init = true;
		}else{
			ESP_LOGE(TAG, "All session files used");
			goto error;
		}

	}else{
		ESP_LOGI(TAG, "Opening session file for update: %s (ocpp)", file_path);
		fp = fopen(file_path, "rb+");
	}

	if(fp == NULL)
	{
		ESP_LOGE(TAG, "Could not create or open session file '%s': %s", file_path, strerror(errno));
		goto error;
	}

	peek_tainted = true; // Indicate that a new peek is necessary to check for transaction timestamps

	if(is_init){
		uint8_t version = 1;
		if(fwrite(&version, sizeof(uint8_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to write transaction id");
			goto error;
		}

		if(fwrite(&transaction_id, sizeof(int), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to write transaction id");
			goto error;
		}
	}

	if(append){
		if(fseek(fp, 0, SEEK_END) != 0){
			ESP_LOGE(TAG, "Unable to seek to end of file");
			goto error;
		}

		long offset_end = ftell(fp);
		if(offset_end == -1){
			ESP_LOGE(TAG, "Unable to determing current file offset");
		}

		if(offset_end < FILE_TXN_ADDR_METER){
			if(fseek(fp, FILE_TXN_ADDR_METER - offset_end, SEEK_CUR) != 0){
				ESP_LOGE(TAG, "Unable to seek past end of file");
				goto error;
			}
		}
	}else{
		if(fseek(fp, offset, whence) != 0){
			ESP_LOGE(TAG, "Unable to seek to location");
			goto error;
		}
	}

	esp_err_t err;
	if(offset + data_length < OCPP_MAX_FILE_SIZE){
		err = writeSessionData_ocpp(fp, sessionData, data_length);
	}else{
		err = ESP_ERR_INVALID_SIZE;
	}

	ESP_LOGI(TAG, "Written ocpp section: %s | Written %ld - %ld (%ld size)", esp_err_to_name(err), offset, ftell(fp),
		ftell(fp) - offset);
	fclose(fp);
	xSemaphoreGive(offs_lock);

	return err;
error:
	if(fp != NULL)
		fclose(fp);
	xSemaphoreGive(offs_lock);
	return ESP_FAIL;
}

esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return -1;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return -1;
	}

	char buf[22] = {0};
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		xSemaphoreGive(offs_lock);
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
		free(base64SessionData);
		xSemaphoreGive(offs_lock);
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

	xSemaphoreGive(offs_lock);

	return ESP_OK;
}

cJSON * offlineSession_ReadChargeSessionFromFile(int fileNo)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return NULL;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return NULL;
	}

	char buf[22] = {0};
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		xSemaphoreGive(offs_lock);
		return NULL;
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

	int base64SessionDataLen = strlen(base64SessionData);

	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);

	ESP_LOGW(TAG,"Session CRC read control: 0x%X vs 0x%X: %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");

	if(crcRead != crcCalc)
	{
		fclose(sessionFile);
		free(base64SessionData);
		xSemaphoreGive(offs_lock);
		return NULL;
	}

	size_t outLen = 0;
	char *sessionDataCreated = (char*)base64_decode(base64SessionData, base64SessionDataLen, &outLen);

	//printf("%d: %s\n", strlen(base64SessionData), base64SessionData);
	//printf("%d: %.*s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

	cJSON * jsonSession = cJSON_Parse(sessionDataCreated);

	fclose(sessionFile);

	free(base64SessionData);
	free(sessionDataCreated);

	xSemaphoreGive(offs_lock);

	return jsonSession;
}

time_t offlineSession_PeekNextMessageTimestamp_ocpp(){
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return LONG_MAX;

	if(!peek_tainted)
		return peeked_timestamp;

	if(xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock for peeking timestamp");
		return LONG_MAX;
	}
	peek_tainted = false;

	char tmp_reading_path[32];
	strcpy(tmp_reading_path, readingPath_ocpp);
	long tmp_offset = readingOffset_ocpp;

	if(tmp_reading_path[0] == '\0'){ // If we are currently not reading any file
		// Find the oldest file
		time_t timestamp_oldest = offlineSession_FindOldestFile_ocpp();
		if(timestamp_oldest == LONG_MAX){
			peeked_timestamp = LONG_MAX;

			xSemaphoreGive(offs_lock);
			return peeked_timestamp;
		}

		snprintf(tmp_reading_path, sizeof(tmp_reading_path), "%s/%.8lx.bin", tmp_path, timestamp_oldest);
		tmp_offset = FILE_TXN_ADDR_START;
	}

	FILE * fp = fopen(tmp_reading_path, "rb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open reading path '%s' to peek timestamp", tmp_reading_path);
		goto error;
	}

	if(fseek(fp, tmp_offset, SEEK_SET) == -1){
		ESP_LOGE(TAG, "Unable to seek to position for peek");
		goto error;
	}

	if(fread(&peeked_timestamp, sizeof(time_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read timestamp for peek");
		goto error;
	}

	if(tmp_offset == FILE_TXN_ADDR_START && peeked_timestamp == 0){ // Start transaction was never written, look for Meter value instead
		if(fseek(fp, FILE_TXN_ADDR_METER, SEEK_SET) == -1){
			ESP_LOGE(TAG, "Unable to seek to meter position for peek");
			goto error;
		}

		if(fread(&peeked_timestamp, sizeof(time_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read timestamp at meter address for peek");
			if(ferror(fp) != 0)
				goto error;
		}
	}

	if(tmp_offset >= FILE_TXN_ADDR_METER && peeked_timestamp == 0){ // Meter value never written, look for Stop transaction

		if(fseek(fp, FILE_TXN_ADDR_STOP, SEEK_SET) == -1){
			ESP_LOGE(TAG, "Unable to seek to stop transaction position for peek");
			goto error;
		}

		if(fread(&peeked_timestamp, sizeof(time_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read timestamp at stop transaction for peek");
			goto error;
		}
	}

	fclose(fp);
	xSemaphoreGive(offs_lock);
	return peeked_timestamp;

error:
	ESP_LOGE(TAG, "EOF: %s, Error: %s", feof(fp) ? "Yes" : "No", ferror(fp) ? strerror(errno) : "Non");
	fclose(fp);

	remove(tmp_reading_path);
	peek_tainted = true;

	xSemaphoreGive(offs_lock);
	peeked_timestamp = LONG_MAX;
	return LONG_MAX;
}

#define MAX_SESSION_DATA_LENGTH 16384

esp_err_t readSessionData_ocpp(FILE * fp, time_t * timestamp_out, unsigned char ** session_data_out, size_t * session_data_length_out){
	if(fread(timestamp_out, sizeof(time_t), 1, fp) != 1)
		goto error;

	size_t base64_length;
	if(fread(&base64_length, sizeof(size_t), 1, fp) != 1)
		goto error;

	if(base64_length == 0)
		return ESP_ERR_NOT_FOUND;

	if(base64_length > MAX_SESSION_DATA_LENGTH)
		goto error;

	char * base64_buffer = malloc(sizeof(unsigned char) * base64_length);
	if(base64_buffer == NULL)
		goto error;

	if(fread(base64_buffer, sizeof(unsigned char), base64_length, fp) != base64_length){
		free(base64_buffer);
		goto error;
	}

	*session_data_out = base64_decode(base64_buffer, base64_length, session_data_length_out);
	free(base64_buffer);
	if(*session_data_out == NULL)
		goto error;

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1)
		goto error;

	uint32_t crc_calc = crc32_normal(0, base64_buffer, base64_length);

	if(crc != crc_calc)
		return ESP_ERR_INVALID_CRC;

	return ESP_OK;
	//return (crc32_normal(0, *session_data_out, *session_data_length_out) == crc)? ESP_OK : ESP_ERR_INVALID_CRC;

error:
	return (feof(fp) != 0)? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

struct start_transaction_session_data{
	int connector_id;
	int meter_start;
	int reservation_id;
	bool valid_reservation;
	char id_tag[21];
};

esp_err_t sessionDataToStartTransaction_ocpp(FILE * fp, int * connector_id, char * id_tag,
					int * meter_start, int * reservation_id, bool * valid_reservation){

	time_t timestamp;
	size_t data_length;
	struct start_transaction_session_data * data = NULL;

	esp_err_t err = readSessionData_ocpp(fp, &timestamp, (unsigned char **)&data, &data_length);

	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to read start transaction data: %s", esp_err_to_name(err));
		return err;
	}

	*connector_id = data->connector_id;
	*meter_start = data->meter_start;
	*reservation_id = data->reservation_id;
	*valid_reservation = data->valid_reservation;
	strcpy(id_tag, data->id_tag);

	free(data);
	return ESP_OK;
}

esp_err_t sessionDataToMeterValue_ocpp(FILE * fp, unsigned char ** meter_data, size_t * meter_data_length){
	time_t timestamp;

	esp_err_t err = readSessionData_ocpp(fp, &timestamp, meter_data, meter_data_length);

	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to read meter value data: %s", esp_err_to_name(err));
	}

	return err;
}

struct stop_transaction_session_data{
	int meter_stop;
	time_t timestamp;
	char reason[15];
	char id_tag[21];
};

int sessionDataToStopTransaction_ocpp(FILE * fp, char * id_tag, int * meter_stop, time_t * timestamp, char * reason){

	time_t timestamp_save;
	size_t data_length;

	struct stop_transaction_session_data * data = NULL;
	esp_err_t err = readSessionData_ocpp(fp, &timestamp_save, (unsigned char **)&data, &data_length);

	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to read stop transaction data: %s", esp_err_to_name(err));
		return err;
	}

	strcpy(id_tag, data->id_tag);
	*meter_stop = data->meter_stop;
	*timestamp = data->timestamp;
	strcpy(reason, data->reason);

	free(data);
	return ESP_OK;
}

cJSON * offlineSession_ReadNextMessage_ocpp(void ** cb_data){
	FILE * fp = NULL;
	bool should_delete = false;

	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return NULL;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock for reading ocpp message");
		return NULL;
	}

	peek_tainted = true; // Indicate that a new peek is necessary to check for transaction timestamps

	if(readingPath_ocpp[0] == '\0'){ // If we are currently not reading any file
		// Find the oldest file
		time_t timestamp_oldest = offlineSession_FindOldestFile_ocpp();
		if(timestamp_oldest == LONG_MAX){
			ESP_LOGE(TAG, "Request to read next message failed due to no ocpp file");
			goto error;
		}

		snprintf(readingPath_ocpp, sizeof(readingPath_ocpp), "%s/%.8lx.bin", tmp_path, timestamp_oldest);
	}

	errno = 0;

	fp = fopen(readingPath_ocpp, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open reading path to read next ocpp message");
		readingPath_ocpp[0] = '\0';
		goto error;
	}

	uint8_t version;
	int transaction_id;

	if(fread(&version, sizeof(uint8_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read version");
		goto error;
	}

	if(fread(&transaction_id, sizeof(int), 1, fp) != 1){
		ESP_LOGW(TAG, "Unable to read transaction id");
		goto error;
	}

	if(readingOffset_ocpp == LONG_MAX){
		ESP_LOGI(TAG, "Starting first read from current file");

		readingOffset_ocpp = FILE_TXN_ADDR_START;
	}

	cJSON * message = NULL;

	if(readingOffset_ocpp == FILE_TXN_ADDR_START){ // Attempt to read StartTransaction.req
		ESP_LOGI(TAG, "Attempting to read start transaction section");

		if(fseek(fp, readingOffset_ocpp, SEEK_SET) == -1){
			ESP_LOGE(TAG, "Unable to seek to ocpp message");
			goto error;
		}

		int connector_id;
		char id_tag[21];
		int meter_start;
		int reservation_id;
		bool valid_reservation;

		esp_err_t status = sessionDataToStartTransaction_ocpp(fp, &connector_id, id_tag, &meter_start, &reservation_id,
								&valid_reservation);

		if(status == ESP_FAIL){
			ESP_LOGE(TAG, "Unable to read session data for ocpp");
			goto error;

		}else if(status == ESP_ERR_NOT_FOUND){
			ESP_LOGI(TAG, "No StartTransactionRequest on file, will look for meter values");
		}
		else{
			time_t start_transaction_timestamp = LONG_MAX;
			sscanf(readingPath_ocpp, "%s/%lx.bin", tmp_path, &start_transaction_timestamp);
			message = ocpp_create_start_transaction_request(connector_id, id_tag, meter_start,
									(valid_reservation) ? &reservation_id : NULL,
									start_transaction_timestamp);

			int * transaction_id_buffer = malloc(sizeof(int));
			if(transaction_id_buffer == NULL){
				ESP_LOGE(TAG, "Unable to allocate cb data for StartTransaction");
			}else{
				*transaction_id_buffer = transaction_id;
				*cb_data = transaction_id_buffer;
			}
		}

		readingOffset_ocpp = FILE_TXN_ADDR_METER;
	}

	struct ocpp_meter_value_list * value_list = NULL;

	if(message == NULL && readingOffset_ocpp >= FILE_TXN_ADDR_METER){ // Attempt to read meter value
		if(fseek(fp, 0, SEEK_END) == -1){
			ESP_LOGE(TAG, "Unable to seek to end of file");
			goto error;
		}

		long offset_end = ftell(fp);
		if(offset_end == -1){
			ESP_LOGE(TAG, "Unable to set end offset");
			goto error;
		}

		if(readingOffset_ocpp < offset_end){
			ESP_LOGI(TAG, "Attempting to read meter value section");

			if(fseek(fp, readingOffset_ocpp, SEEK_SET) == -1){
				ESP_LOGE(TAG, "Unable to seek to current MeterValue.req");
				goto error;
			}

			unsigned char * meter_data = NULL;
			size_t meter_data_length;
			esp_err_t state = sessionDataToMeterValue_ocpp(fp, &meter_data, &meter_data_length);

			if(state == ESP_OK && meter_data != NULL){
				bool is_stop_txn_data;
				value_list = ocpp_meter_list_from_contiguous_buffer(meter_data, meter_data_length, &is_stop_txn_data);
				free(meter_data);

				if(value_list == NULL){
					ESP_LOGE(TAG, "Unable to create value list from meter data");
					goto error;
				}

				if(is_stop_txn_data){
					ESP_LOGI(TAG, "Read meter value is stop transaction data, checking for stop transaction request");
					readingOffset_ocpp = FILE_TXN_ADDR_STOP;
				}else{
					ESP_LOGI(TAG, "Read meter value");
					message = ocpp_create_meter_values_request(1, &transaction_id, value_list);
					*cb_data = "Meter value";

					if(message == NULL){
						ESP_LOGE(TAG, "unable to create meter value request");
					}

					readingOffset_ocpp = ftell(fp);
				}

			}else if(state == ESP_ERR_NOT_FOUND){
				ESP_LOGI(TAG, "No meter values stored, checking for stop transaction message");

			}else{
				ESP_LOGE(TAG, "Unable to get meter data from session data");
				goto error;
			}
		}
	}

	if(message == NULL){ // Attempt to read stop transaction
		should_delete = true; // stop transaction is the last message attempted to be read, therefore file should be deleted
		ESP_LOGI(TAG, "Attempting to read stop transaction section");

		if(fseek(fp, FILE_TXN_ADDR_STOP, SEEK_SET) == -1){
			ESP_LOGE(TAG, "Unable to seek to stop transaction");
			ocpp_meter_list_delete(value_list);
			goto error;
		}

		char id_tag[21];
		int meter_stop;
		time_t timestamp;
		char reason[16];

		esp_err_t state = sessionDataToStopTransaction_ocpp(fp, id_tag, &meter_stop, &timestamp, reason);
		if(state == ESP_OK){
			ESP_LOGI(TAG, "Read stop transaction, creating message");
			message = ocpp_create_stop_transaction_request(id_tag, meter_stop, timestamp, &transaction_id, reason, value_list);
			*cb_data = "stop";

		}else if(state == ESP_ERR_NOT_FOUND){
			if(value_list != NULL){
				ESP_LOGI(TAG, "No stop transaction written, creating meter value message");
				message = ocpp_create_meter_values_request(1, &transaction_id, value_list);
				*cb_data = "Meter value";
			}else{
				ESP_LOGI(TAG, "All transaction messages read for %d", transaction_id);
			}
		}else{
			ESP_LOGE(TAG, "Unable to get stop transaction from session data");
			ocpp_meter_list_delete(value_list);
			goto error;
		}
	}

	ocpp_meter_list_delete(value_list);

	if(!should_delete){ // Check if there are more messages in transaction
		ESP_LOGI(TAG, "Checking for more messages in current offline transaction file");
		time_t next_timestamp = 0;
		if(fseek(fp, readingOffset_ocpp, SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to next transaction message, deleting file");
			should_delete = true;
		}
		else if(fread(&next_timestamp, sizeof(time_t), 1, fp) != 1 || next_timestamp < OCPP_MIN_TIMESTAMP){
			if(readingOffset_ocpp >= FILE_TXN_ADDR_METER){
				ESP_LOGI(TAG, "No more meter values found");

				readingOffset_ocpp = FILE_TXN_ADDR_STOP;
				if(fseek(fp, readingOffset_ocpp, SEEK_SET) != 0){
					ESP_LOGE(TAG, "Unable to seek to stop transaction");
					should_delete = true;
				}else{
					if(fread(&next_timestamp, sizeof(time_t), 1, fp) != 1 || next_timestamp < OCPP_MIN_TIMESTAMP){
						ESP_LOGI(TAG, "No stop transaction found");
						should_delete = true;
					}
				}
			}
		}
	}

	fclose(fp);
	if(should_delete){
		ESP_LOGI(TAG, "Deleting transaction file");

		remove(readingPath_ocpp);
		readingPath_ocpp[0] = '\0';
		readingOffset_ocpp = LONG_MAX;
	}

	xSemaphoreGive(offs_lock);

	return message;
error:
	if(fp != NULL){
		if(feof(fp) != 0){
			if(ftell(fp) >= FILE_TXN_ADDR_METER){
				ESP_LOGE(TAG, "File stream reports EOF. Moving to stop transaction");
				readingOffset_ocpp = FILE_TXN_ADDR_STOP;

			}else{
				ESP_LOGE(TAG, "File stream reports EOF and not part of meter value section, marking for deletion");
				should_delete = true;
			}

		}else{

			if(ferror(fp) != 0){
				ESP_LOGE(TAG, "File stream reports error. Errno set to %s", strerror(errno));
			}
			else{
				ESP_LOGE(TAG, "File stream reports no error.");
			}

			if(readingOffset_ocpp == FILE_TXN_ADDR_START){
				ESP_LOGW(TAG, "Error was during reading of start transaction, moving to meter values");
				readingOffset_ocpp = FILE_TXN_ADDR_METER;

			}else if(readingOffset_ocpp >= FILE_TXN_ADDR_METER){
				ESP_LOGE(TAG, "Error was during reading of meter value, moving to stop transaction");
				readingOffset_ocpp = FILE_TXN_ADDR_STOP;

			}else if(readingOffset_ocpp == LONG_MAX){
				ESP_LOGE(TAG, "Error while readingOffset was not set");
				should_delete = true;

			}else{
				ESP_LOGE(TAG, "Error was during stop transaction, marking file for deletion");
				should_delete = true;
			}
		}

		fclose(fp);
	}

	if(should_delete){
		ESP_LOGE(TAG, "File was marked for deletion; Deleting file");
		remove(readingPath_ocpp);
		readingPath_ocpp[0] = '\0';
		readingOffset_ocpp = LONG_MAX;
	}

	xSemaphoreGive(offs_lock);
	return NULL;
}

static double startEnergy = 0.0;
static double stopEnergy = 0.0;

double GetEnergySigned()
{
	double signedDiff = stopEnergy - startEnergy;

	//Clear variables
	startEnergy = 0;
	stopEnergy = 0;
	return signedDiff;
}


cJSON* offlineSession_GetSignedSessionFromActiveFile(int fileNo)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return NULL;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return NULL;
	}

	char buf[22] = {0};
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		xSemaphoreGive(offs_lock);
		return NULL;
	}

	cJSON * entryArray = cJSON_CreateArray();

	uint8_t fileVersion = 0;
	fread(&fileVersion, 1, 1, sessionFile);
	ESP_LOGW(TAG, "File version: %d", fileVersion);



	/// Go to SignedSession length index
	fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

	uint32_t nrOfOCMFElements = 0;
	fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
	ESP_LOGI(TAG, "NrOfElements read: %i", nrOfOCMFElements);

	/// Build OCMF strings for each element, Should never be more than maxElements
	if((nrOfOCMFElements > 0) && (nrOfOCMFElements <= max_offline_signed_values))
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
				if(OCMFElement.label == 'B')
					startEnergy = OCMFElement.energy;

				if(OCMFElement.label == 'E')
					stopEnergy = OCMFElement.energy;

				cJSON * logArrayElement = cJSON_CreateObject();
				char timeBuffer[50] = {0};
				zntp_format_time(timeBuffer, OCMFElement.timestamp);

				///Convert char to string for Json use. must have \0 ending.
				char tx[2] = {OCMFElement.label, 0};

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

	xSemaphoreGive(offs_lock);

	return entryArray;
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

	if(!offlineSession_select_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	/// Search for file number to use for this session
	activeFileNumber = offlineSession_FindNewFileNumber();
	if(activeFileNumber < 0)
	{
		return ESP_FAIL;
	}

	sprintf(activePathString,"%s/%d.bin", tmp_path, activeFileNumber);

	//Save the session structure to the file including the start 'B' message
	offlineSession_UpdateSessionOnFile(sessionData, true);

	return ret;
}

esp_err_t offlineSession_SaveStartTransaction_ocpp(int transaction_id, time_t transaction_start_timestamp, int connector_id,
						const char * id_tag, int meter_start, int * reservation_id)
{
	if(!offlineSession_select_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline ocpp log will not work");
		return ESP_FAIL; //NOTE: The Save*_ocppx functions return ESP_FAIL when unable to mount SaveSession returns 0 (ESP_OK)
	}

	struct start_transaction_session_data data = {
		.connector_id = connector_id,
		.meter_start = meter_start,
	};

	if(id_tag == NULL){
		ESP_LOGE(TAG, "Missing id tag for start transaction");
		return ESP_ERR_INVALID_ARG;
	}
	strncpy(data.id_tag, id_tag, sizeof(data.id_tag));


	if(reservation_id != NULL){
		data.reservation_id = *reservation_id;
		data.valid_reservation = true;
	}else{
		data.reservation_id = -1;
		data.valid_reservation = false;
	}

	ESP_LOGW(TAG, "Writing start transaction");
	return offlineSession_UpdateSessionOnFile_Ocpp((unsigned char *)&data, sizeof(struct start_transaction_session_data), transaction_id,
						transaction_start_timestamp, FILE_TXN_ADDR_START, SEEK_SET, false);
}

esp_err_t offlineSession_SaveStopTransaction_ocpp(int transaction_id, time_t transaction_start_timestamp, const char * id_tag,
						int meter_stop, time_t timestamp, const char * reason)
{
	if(!offlineSession_select_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline ocpp log will not work");
		return ESP_FAIL;
	}

	struct stop_transaction_session_data data = {
		.meter_stop = meter_stop,
		.timestamp = timestamp
	};

	if(id_tag != NULL){
		strncpy(data.id_tag, id_tag, sizeof(data.id_tag));
	}else{
		data.id_tag[0] = '\0';
	}

	if(reason != NULL){
		strncpy(data.reason, reason, sizeof(data.reason));
	}else{
		data.reason[0] = '\0';
	}

	ESP_LOGW(TAG, "Writing stop transaction");
	return offlineSession_UpdateSessionOnFile_Ocpp((unsigned char *)&data, sizeof(struct stop_transaction_session_data), transaction_id,
						transaction_start_timestamp, FILE_TXN_ADDR_STOP, SEEK_SET, false);
}

esp_err_t offlineSession_SaveNewMeterValue_ocpp(int transaction_id, time_t transaction_start_timestamp, const unsigned char * meter_data, size_t buffer_length)
{
	if(!offlineSession_select_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline ocpp log will not work");
		return ESP_FAIL;
	}

	return offlineSession_UpdateSessionOnFile_Ocpp(meter_data, buffer_length, transaction_id,
						transaction_start_timestamp, FILE_TXN_ADDR_METER, SEEK_SET, true);
}

esp_err_t offlineSession_UpdateTransactionId_ocpp(int old_transaction_id, int new_transaction_id){
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return ESP_FAIL;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock for updating transaction id");
		return ESP_FAIL;
	}

	DIR * dir = opendir(tmp_path);
	if(dir == NULL){
		ESP_LOGE(TAG, "Unable to open directory (%s) to update transaction id", tmp_path);
		xSemaphoreGive(offs_lock);
		return -1;
	}

        struct dirent * dp = readdir(dir);
	char file_path[19];
	bool found_transaction = false;

	while(dp != NULL){
		if(dp->d_type == DT_REG){
			long timestamp = strtol(dp->d_name, NULL, 16); // filename for ocpp transactions are based on unix time IN hexadecimal
			if(timestamp > OCPP_MIN_TIMESTAMP && timestamp != LONG_MAX){
				int written_length = snprintf(file_path, sizeof(file_path), "%s/%s", tmp_path, dp->d_name);

				if(written_length > 0 && written_length < sizeof(file_path)){
					FILE * fp = fopen(file_path, "rb+");

					if(fp != NULL && fseek(fp, FILE_TXN_ADDR_ID, SEEK_SET) == 0){
						int transaction_id;

						if(fread(&transaction_id, sizeof(int), 1, fp) == 1){
							if(transaction_id == old_transaction_id){
								found_transaction = true;
								//TODO: Check if long offset in fseek is equal to size_t size in fread
								if(fseek(fp, -sizeof(int), SEEK_CUR) == 0){
									if(fwrite(&new_transaction_id, sizeof(int), 1, fp) != 1){
										ESP_LOGE(TAG, "Unable to write new transaction id");
									}
								}else{
									ESP_LOGE(TAG, "Unable to seek back to transaction id");
								}
							}
						}else{
							ESP_LOGE(TAG, "Unable to read transaction id for update");
						}
					}else{
						ESP_LOGE(TAG, "Unable to %s for id update", (fp == NULL) ? "open file" : "seek");
					}

					if(fp != NULL)
						fclose(fp);

				}else{
					ESP_LOGE(TAG, "Unable to create filepath from d_name");
				}
			}
		}

		if(found_transaction)
			break;

		dp = readdir(dir);
	}

	closedir(dir);

	xSemaphoreGive(offs_lock);
	return (found_transaction) ? ESP_OK : ESP_ERR_NOT_FOUND;
}


void offlineSession_append_energy(char label, int timestamp, double energy)
{
	struct stat st;
	if(stat(tmp_path, &st) != 0)
		return;

	if(activeFileNumber < 0)
		return;

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return;
	}

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
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}

			/// Make sure no attempts to write energy occurs after the End label is set
			if(label == 'E')
				offlineSessionOpen = false;


			/// Get nr of elements...
			fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);

			/// Should at least be one element 'B'
			if(nrOfOCMFElements == 0)
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %d (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}

			///100 elements, leave room for last element with 'E'
			if((label == 'T') && (nrOfOCMFElements >= (max_offline_signed_values-1)))
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %d (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}

			///Max 100 elements check if room for 'e'
			if((label == 'E') && (nrOfOCMFElements >= max_offline_signed_values))
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %d (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
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

	xSemaphoreGive(offs_lock);
}

/*int offlineSession_ReadOldestSession(char * SessionString)
{
	if(mounted == false)
		return -1;

	sessionFile = fopen(activePathString, "rb+");

	if(sessionFile != NULL)
	{
		/// Find end of file to get size
		fseek(sessionFile, 0L, SEEK_END);
		size_t readSize = ftell(sessionFile);
		ESP_LOGW(TAG, "FileNo %d: filesize: %d", activeFileNumber, readSize);

		fseek(sessionFile, 1000, SEEK_SET);
	}

	return 0;
}*/

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


void offlineSession_DeleteAllFiles()
{
	ESP_LOGW(TAG, "Deleting all files");
	int fileNo;
	for (fileNo = 0; fileNo < max_offline_session_files; fileNo++)
	{
		offlineSession_delete_session(fileNo);
	}
}

// Returns number of files not deleted
int offlineSession_DeleteAllFiles_ocpp()
{
	ESP_LOGW(TAG, "Deleting all files (ocpp)");

	char full_path[20];
	int nr_of_unsuccessfull_removals = 0;
	DIR * dir = opendir(tmp_path);

	struct dirent * dp = readdir(dir);

	while(dp != NULL){
		if(dp->d_type == DT_REG){
			long timestamp = strtol(dp->d_name, NULL, 16);
			if(timestamp > OCPP_MIN_TIMESTAMP && timestamp != LONG_MAX){
				if(strnlen(dp->d_name, 13) == 12){
					snprintf(full_path, sizeof(full_path), "%s/%.12s", tmp_path, dp->d_name);
				}else{
					ESP_LOGE(TAG, "Unexpected file length, '%s' not a 8.3 filename", dp->d_name);
					dp = readdir(dir);
					continue;
				}

				ESP_LOGW(TAG, "Deleting ocpp file: %s", full_path);
				if(remove(full_path) != 0){
					ESP_LOGE(TAG, "Unable to delete %s: %s", full_path, strerror(errno));
					nr_of_unsuccessfull_removals++;
				}
			}
		}

		dp = readdir(dir);
	}

	closedir(dir);

	return nr_of_unsuccessfull_removals;
}

int offlineSession_delete_session(int fileNo)
{
	int ret = 0;

	if(!offlineSession_select_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return -1;
	}

	char buf[22] = {0};
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);

	FILE *fp = fopen(buf, "r");
	if(fp==NULL)
	{
		ESP_LOGE(TAG, "File %d: Before remove: logfile can't be opened ", fileNo);
		xSemaphoreGive(offs_lock);
		return 0;
	}
	else
		ESP_LOGI(TAG, "File %d: Before remove: logfile can be opened ", fileNo);

	fclose(fp);

	remove(buf);

	fp = fopen(buf, "r");
	if(fp==NULL)
	{
		ESP_LOGI(TAG, "File %d: After remove: logfile deleted SUCCEEDED", fileNo);
		ret = 1;
	}
	else
	{
		ESP_LOGE(TAG, "File %d: After remove: logfile delete FAILED ", fileNo);
		ret = 2;
	}

	fclose(fp);

	xSemaphoreGive(offs_lock);

	return ret;
}
