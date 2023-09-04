#include "offlineSession.h"

#define TAG "OFFLINE_SESSION"

#include "esp_log.h"
#include "errno.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "base64.h"
#include "fat.h"

#include "OCMF.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_protocol_serialisation.h"
#include "chargeSession.h"
#include "../components/ntp/zntp.h"
#include "../components/i2c/include/i2cDevices.h"
#include "offline_log.h"

static char tmp_path[] = "/files";

static const int max_offline_session_files = 100;
static const int max_offline_signed_values = 100;

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

	// Don't use directly, read and write through provided functions:
	//
	// offlineSession_ReadTimestamp
	// offlineSession_SetTimestamp
	//
	uint32_t _timestamp;
	double energy;
	uint32_t crc;

	// There was 4 bytes of padding at end of LogOCMFData, we can use this as the
	// high dword of the timestamp while keeping the size of the struct the same.
	//
	// On downgrade, this will be ignored, treated as padding.
	//
	// On upgrade, we can still potentially handle files with version 1 (though they
	// should be sent to the cloud already).
	//
	uint32_t _timestampHigh;
};

static SemaphoreHandle_t offs_lock;
static TickType_t lock_timeout = pdMS_TO_TICKS(1000*5);

static bool offlineSessionOpen = false;

static int activeFileNumber = -1;
static char activePathString[32] = {0};
static FILE *sessionFile = NULL;
static int maxOfflineSessionsCount = 0;
static bool disabledForCalibration = false;

#define FILE_DIAG_BUF_SIZE 150
static char fileDiagnostics[FILE_DIAG_BUF_SIZE] = {0};

#define MAX_SEQ_DIAG_LEN 250
static char sequenceDiagnostics[MAX_SEQ_DIAG_LEN] = {0};

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

void offlineSession_AppendLogString(char * stringToAdd)
{
	int seqDiagLen = 0;
	if(sequenceDiagnostics[0])
		seqDiagLen = strlen(sequenceDiagnostics);

	snprintf(sequenceDiagnostics + seqDiagLen, MAX_SEQ_DIAG_LEN - seqDiagLen, "%s\r\n", stringToAdd);
}

void offlineSession_AppendLogStringWithInt(char * stringToAdd, int value)
{
	int seqDiagLen = 0;
	if(sequenceDiagnostics[0])
		seqDiagLen = strlen(sequenceDiagnostics);

	snprintf(sequenceDiagnostics + seqDiagLen, MAX_SEQ_DIAG_LEN - seqDiagLen, "%s %i\r\n", stringToAdd, value);
}

void offlineSession_AppendLogStringWithIntInt(char * stringToAdd, int value1, int value2)
{
	int seqDiagLen = 0;
	if(sequenceDiagnostics[0])
		seqDiagLen = strlen(sequenceDiagnostics);

	snprintf(sequenceDiagnostics + seqDiagLen, MAX_SEQ_DIAG_LEN - seqDiagLen, "%s %i %i\r\n", stringToAdd, value1, value2);
}

void offlineSession_AppendLogStringErr()
{
	int seqDiagLen = 0;
	if(sequenceDiagnostics[0])
		seqDiagLen = strlen(sequenceDiagnostics);

	snprintf(sequenceDiagnostics + seqDiagLen, MAX_SEQ_DIAG_LEN - seqDiagLen, "%i:%s\r\n", errno, strerror(errno));
}


void offlineSession_AppendLogLength()
{
	int len = 0;
	if(sequenceDiagnostics[0])
		len = strlen(sequenceDiagnostics);

	int seqDiagLen = 0;
	if(sequenceDiagnostics[0])
		seqDiagLen = strlen(sequenceDiagnostics);

	snprintf(sequenceDiagnostics + seqDiagLen, MAX_SEQ_DIAG_LEN - seqDiagLen, "(%i)", len);
}

char * offlineSession_GetLog()
{
	return sequenceDiagnostics;
}

void offlineSession_ClearLog()
{
	memset(sequenceDiagnostics, 0, sizeof(sequenceDiagnostics));
}


bool offlineSession_CheckFilesSystem()
{
	bool deletedOK = false;
	bool createdOK = offlineSession_test_CreateFile();
	if(createdOK)
		deletedOK = offlineSession_test_DeleteFile();

	int fileDiagLen = 0;
	if(fileDiagnostics[0])
		fileDiagLen = strlen(fileDiagnostics);

	snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " Disk file: created = %i, deleted = %i,", createdOK, deletedOK);

	return deletedOK; //True if both bools are OK
}

void offlineSession_disable(void) {
	disabledForCalibration = true;
}

bool offlineSession_is_mounted(void) {
	if (disabledForCalibration) {
		ESP_LOGI(TAG, "Blocking offline sessions during calibration!");
		return false;
	}

	struct stat st;
	return stat(tmp_path, &st) == 0;
}

static inline uint64_t offlineSession_ReadTimestamp(struct LogOCMFData *log, int version) {
	uint64_t timestamp;
	if (version == 2) {
		timestamp = ((uint64_t)log->_timestampHigh << 32) | log->_timestamp;
	} else {
		// High dword (padding) is probably zero anyway but to be sure, just use low dword.
		timestamp = log->_timestamp;
	}
	return timestamp;
}

static inline void offlineSession_SetTimestamp(struct LogOCMFData *log, int version, uint64_t timestamp) {
	if (version == 2) {
		log->_timestamp = timestamp & 0xffffffff;
		log->_timestampHigh = (timestamp >> 32) & 0xffffffff;
	} else {
		log->_timestamp = timestamp & 0xffffffff;
		log->_timestampHigh = 0;
	}
}

static FILE *testFile = NULL;
bool offlineSession_test_CreateFile()
{
	if(!offlineSession_is_mounted()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return false;
	}

	errno = 0; // Clear errno
	testFile = fopen("/files/testfile.bin", "wb+");

	if((testFile == NULL) || (errno != 0))
		ESP_LOGE(TAG, "#### Create file errno: fp: 0x%p %i: %s", testFile, errno, strerror(errno));

	int fileDiagLen = 0;

	if(testFile == NULL)
	{
		if(fileDiagnostics[0])
			fileDiagLen = strlen(fileDiagnostics);

		snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, "New file = NULL, %i:%s,", errno, strerror(errno));
		return false;
	}
	else
	{
		if(fileDiagnostics[0])
			fileDiagLen = strlen(fileDiagnostics);

		snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, "New file = 0x%08x", (unsigned int)testFile);

		uint8_t value = 0xA;
		size_t wret = fwrite((void*)&value, sizeof(uint8_t), 1, testFile);

		value = 0;
		fseek(testFile, 0, SEEK_SET);

		size_t rret = fread((void*)&value, sizeof(uint8_t), 1, testFile);

		fclose(testFile);

		if((rret != 1) || (value != 0xA))
		{
			ESP_LOGE(TAG, "fread verification failed: 0x%x, w:%i r:%i", value, wret, rret);
			ESP_LOGE(TAG, "#### RW file errno: fp: 0x%p %i: %s", testFile, errno, strerror(errno));

			if(fileDiagnostics[0])
				fileDiagLen = strlen(fileDiagnostics);

			snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, "Read failed: %i", (int)rret);
			return false;
		}

		ESP_LOGI(TAG, "fread verification OK: 0x%x, %i", value, rret);
	}

	return true;
}


/*bool offlineSession_test_DeleteFile()
{
	if(!offlineSession_mount_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return false;
	}

	FILE *fp = fopen("/files/testfile.bin", "r");
	if(fp==NULL)
	{
		snprintf(fileDiagnostics + strlen(fileDiagnostics), FILE_DIAG_BUF_SIZE, " File can't be opened,");
		ESP_LOGI(TAG, "File before remove: can't be opened ");
		return false;
	}
	else
	{
		snprintf(fileDiagnostics + strlen(fileDiagnostics), FILE_DIAG_BUF_SIZE, " File can be opened,");
		ESP_LOGI(TAG, "File before remove: can be opened ");
	}

	fclose(fp);

	remove("/files/testfile.bin");

	fp = fopen("/files/testfile.bin", "r");
	if(fp==NULL)
	{
		snprintf(fileDiagnostics + strlen(fileDiagnostics), FILE_DIAG_BUF_SIZE, " File deleted OK");
		ESP_LOGI(TAG, "File after remove: delete SUCCEEDED");
	}
	else
	{
		snprintf(fileDiagnostics + strlen(fileDiagnostics), FILE_DIAG_BUF_SIZE, " File deleted FAILED");
		ESP_LOGE(TAG, "File after remove: delete FAILED");
	}

	fclose(fp);

	return true;
}*/


bool offlineSession_test_DeleteFile()
{
	if(!offlineSession_is_mounted()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return false;
	}

	int status = remove("/files/testfile.bin");
	ESP_LOGW(TAG, "Status from remove: %i", status);
	if(status == 0)
	{
		return true;
	}
	else
	{
		int fileDiagLen = 0;
		if(fileDiagnostics[0])
			fileDiagLen = strlen(fileDiagnostics);

		snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " remove = %i, %i:%s ", status, errno, strerror(errno));
	}

	return false;
}


void offlineSession_ClearDiagnostics()
{
	memset(fileDiagnostics, 0, FILE_DIAG_BUF_SIZE);
}

char * offlineSession_GetDiagnostics()
{
	return fileDiagnostics;
}

static bool isFileSystemOK = false;
/// Intended for check during factory test
bool offlineSession_FileSystemVerified()
{
	return isFileSystemOK;
}

static bool fileSystemCorrected = false;
bool offlineSession_FileSystemCorrected()
{
	return fileSystemCorrected;
}

/*
 * Some chargers has not been able to create new files on the "files" partition
 * This function checks is file creation returns NULL, and if so formats and remounts the
 * file system to correct the problem.
 */
bool offlineSession_CheckAndCorrectFilesSystem()
{
	int fileDiagLen = 0;
	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		if(fileDiagnostics[0])
			fileDiagLen = strlen(fileDiagnostics);

		snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, "Semaphore fault");
		ESP_LOGE(TAG, "failed to obtain offs lock during check and correct");
		return false;
	}

	///Test file system by creating one test-file
	isFileSystemOK = offlineSession_test_CreateFile();

	if(isFileSystemOK == false)
	{
		ESP_LOGE(TAG, "FILE SYSTEM FAULTY");
		isFileSystemOK = offlineSession_eraseAndRemountPartition();

		if(isFileSystemOK)
		{
			if(fileDiagnostics[0])
				fileDiagLen = strlen(fileDiagnostics);

			snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " M: %i,", offlineSession_is_mounted());

			isFileSystemOK = offlineSession_test_CreateFile();
			if(isFileSystemOK)
			{
				fileSystemCorrected = offlineSession_test_DeleteFile();

				if(fileDiagnostics[0])
					fileDiagLen = strlen(fileDiagnostics);

				snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " fileSystemCorrected: %i", fileSystemCorrected);
			}
		}
		else
		{
			if(fileDiagnostics[0])
				fileDiagLen = strlen(fileDiagnostics);

			snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " EraseAndRemount failed");
		}
		ESP_LOGW(TAG, "FILE SYSTEM CORRECTED");
	}
	else
	{
		bool deletedOK = offlineSession_test_DeleteFile();

		if(fileDiagnostics[0])
			fileDiagLen = strlen(fileDiagnostics);

		snprintf(fileDiagnostics + fileDiagLen, FILE_DIAG_BUF_SIZE - fileDiagLen, " Removed: %i", deletedOK);

		if(deletedOK == false)
			ESP_LOGE(TAG, "Removing testfile failed");
	}

	xSemaphoreGive(offs_lock);

	return isFileSystemOK;
}

bool offlineSession_eraseAndRemountPartition()
{
	int fileDiagLen = 0;
	if(fileDiagnostics[0])
		fileDiagLen = strlen(fileDiagnostics);

	return fat_eraseAndRemountPartition(eFAT_ID_FILES, fileDiagnostics, FILE_DIAG_BUF_SIZE, fileDiagLen) == ESP_OK;
}

/*
 * Find the file to use for a new session
 */
int offlineSession_FindNewFileNumber()
{
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return -1;
	}

	int fileNo = 0;
	FILE *file;
	char buf[32] = {0};

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
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return -1;
	}

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


int offlineSession_FindNrOfFiles()
{
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return -1;
	}

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

int offlineSession_GetMaxSessionCount()
{
	int tmp = maxOfflineSessionsCount;
	maxOfflineSessionsCount = 0;
	return tmp;
}

int offlineSession_CheckIfLastLessionIncomplete(struct ChargeSession *incompleteSession)
{
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return -1;
	}

	int fileNo = 0;
	FILE *lastUsedFile;
	char buf[32] = {0};

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

int offlineSession_UpdateSessionOnFile(char *sessionData, bool createNewFile)
{
	ESP_LOGE(TAG, " *** UpdateSessionOnFile: %i ***", createNewFile);

	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return ESP_FAIL;
	}

	if(activeFileNumber < 0)
		return ESP_FAIL;


	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return ESP_FAIL;
	}

	if(createNewFile)
		sessionFile = fopen(activePathString, "wb+");
	else
		sessionFile = fopen(activePathString, "rb+");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Could not create or open sessionFile %s (%i)", activePathString, createNewFile);
		if(createNewFile)
		{
			offlineSession_AppendLogString("1 sessionFile = NULL");
			offlineSession_AppendLogStringErr();
		}
		else
		{
			offlineSession_AppendLogString("2 Reopen sessionFile = NULL");
			offlineSession_AppendLogStringErr();
		}

		xSemaphoreGive(offs_lock);
		return -2;
	}

	//ESP_LOGW(TAG, "strlen: %d, path: %s", strlen(sessionData), activePathString);

	// Write version 2 for new sessions, which support 64-bit timestamp
	uint8_t fileVersion = 2;
	fseek(sessionFile, FILE_VERSION_ADDR_0, SEEK_SET);
	fwrite(&fileVersion, 1, 1, sessionFile);

	int sessionDataLen = strlen(sessionData);
	size_t outLen = 0;
	char * base64SessionData = base64_encode(sessionData, sessionDataLen, &outLen);
	volatile int base64SessionDataLen = strlen(base64SessionData);

	ESP_LOGW(TAG,"%d: %s\n", strlen(sessionData), sessionData);
	//ESP_LOGW(TAG,"%d: %s\n", strlen(base64SessionData), base64SessionData);
	if(base64SessionDataLen != outLen)
	{
		ESP_LOGE(TAG,"##### %i != %i ######", base64SessionDataLen, outLen);
		offlineSession_AppendLogStringWithIntInt("1 base64EncodeLen vs outLen", base64SessionDataLen, outLen);
	}
	/*else
	{
		ESP_LOGI(TAG,"***** %i == %i ******", base64SessionDataLen, outLen);
	}*/

	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);
	size_t wr1 = fwrite(base64SessionData, base64SessionDataLen, 1, sessionFile);

	///Write CRC at the end of the block
	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);
	fseek(sessionFile, FILE_SESSION_CRC_ADDR_996, SEEK_SET);
	size_t wr2 = fwrite(&crcCalc, sizeof(uint32_t), 1, sessionFile);

	//ESP_LOGW(TAG, "Session CRC:: 0x%X", crcCalc);

	free(base64SessionData);
	fclose(sessionFile);

	if(createNewFile)
	{
		//Added diagnostics to try and find cause of empty sessions.
		offlineSession_AppendLogStringWithIntInt("1 sessionFile created", wr1, wr2);
	}

	xSemaphoreGive(offs_lock);

	return ESP_OK;
}

esp_err_t offlineSession_Diagnostics_ReadFileContent(int fileNo)
{
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return -1;
	}

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return -1;
	}

	char buf[32] = {0};
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


	char * base64SessionData = calloc(1000-6, 1);
	fread(base64SessionData, 1000-6, 1, sessionFile);

	int readLen = strlen(base64SessionData);
	uint32_t crcCalc = 0;
	if(readLen <= 996)
		crcCalc = crc32_normal(0, base64SessionData, readLen);

	ESP_LOGW(TAG,"Session CRC read control: 0x%" PRIX32 " vs 0x%" PRIX32 ": %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");

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
	ESP_LOGI(TAG, "NrOfElements read: %" PRIi32 "", nrOfOCMFElements);

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

			uint64_t timestamp = offlineSession_ReadTimestamp(&OCMFElement, fileVersion);

			/// Hold'n clear crc to get correct calculation for packet
			uint32_t packetCrc = OCMFElement.crc;
			OCMFElement.crc = 0;

			uint32_t crcCalc = crc32_normal(0, &OCMFElement, sizeof(struct LogOCMFData));

			ESP_LOGW(TAG, "OCMF read %i addr: %i : %c %" PRIu64 " %f 0x%" PRIX32 " %s", i, newElementPosition, OCMFElement.label, timestamp, OCMFElement.energy, packetCrc, (crcCalc == packetCrc) ? "MATCH" : "FAIL");
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
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return NULL;
	}

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		offlineSession_AppendLogString("3 Read:CS SEM FAIL");
		return NULL;
	}

	char buf[32] = {0};
	sprintf(buf,"%s/%d.bin", tmp_path, fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		offlineSession_AppendLogString("3 Read:sessionFile == NULL");
		offlineSession_AppendLogStringErr();
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

	char * base64SessionData = calloc(1000-6, 1);
	fread(base64SessionData, 1000-6, 1, sessionFile);

	int base64SessionDataLen = strlen(base64SessionData);

	uint32_t crcCalc = crc32_normal(0, base64SessionData, base64SessionDataLen);

	ESP_LOGW(TAG,"Session CRC read control: 0x%" PRIX32 " vs 0x%" PRIX32 ": %s", crcRead, crcCalc, (crcRead == crcCalc) ? "MATCH" : "FAIL");

	if(crcRead != crcCalc)
	{
		fclose(sessionFile);
		free(base64SessionData);
		xSemaphoreGive(offs_lock);
		offlineSession_AppendLogStringWithIntInt("3 Read:CS CRC FAIL", (int)crcRead, (int)crcCalc);
		return NULL;
	}

	offlineSession_AppendLogStringWithIntInt("3 Read:CS CRC OK", (int)crcRead, (int)crcCalc);

	size_t outLen = 0;
	char *sessionDataCreated = (char*)base64_decode(base64SessionData, base64SessionDataLen, &outLen);

	char *sessionData = NULL;

	if(sessionDataCreated != NULL)
	{
		/// Sanity check
		if(outLen <= 1000)
		{
			//int sessionLen = strlen(sessionDataCreated);
			//printf("%d:%d: %s\n", strlen(base64SessionData), outLen, base64SessionData);
			//printf("%d:%d %s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

			sessionData = malloc(outLen+1);//, sizeof(char));
			if(sessionData != NULL)
			{
				//ESP_LOGE(TAG,"%d:%d %s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

				memset(sessionData, 0, outLen+1);
				strncpy(sessionData, sessionDataCreated, outLen);
				ESP_LOGW(TAG,"%d->%d==%d %s\n",strlen(sessionDataCreated), outLen, strlen(sessionData), sessionData);
				offlineSession_AppendLogStringWithInt("3 SessLen: ", outLen);
			}
			else
			{
				offlineSession_AppendLogString("3 NULL from malloc");
			}
		}
		else
		{
			offlineSession_AppendLogString("3 outLen > 1000");
		}
	}
	else
	{
		offlineSession_AppendLogString("3 NULL from base64_decode");
	}
	//printf("%d:%d %.*s\n", strlen(sessionDataCreated), outLen, outLen, sessionDataCreated);

	cJSON * jsonSession = cJSON_Parse(sessionDataCreated);

	free(sessionData);

	fclose(sessionFile);

	free(base64SessionData);
	free(sessionDataCreated);

	xSemaphoreGive(offs_lock);

	return jsonSession;
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
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "files is not mounted!");
		return NULL;
	}

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return NULL;
	}

	char buf[32] = {0};
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
	ESP_LOGI(TAG, "NrOfElements read: %" PRIi32 "", nrOfOCMFElements);

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

			uint64_t timestamp = offlineSession_ReadTimestamp(&OCMFElement, fileVersion);

			/// Hold'n clear crc got get correct calculation for packet
			uint32_t packetCrc = OCMFElement.crc;
			OCMFElement.crc = 0;

			uint32_t crcCalc = crc32_normal(0, &OCMFElement, sizeof(struct LogOCMFData));

			ESP_LOGW(TAG, "OCMF read %i addr: %i : %c %" PRIu64 " %f 0x%" PRIX32 " %s", i, newElementPosition, OCMFElement.label, timestamp, OCMFElement.energy, packetCrc, (crcCalc == packetCrc) ? "MATCH" : "FAIL");

			if((crcCalc == packetCrc))
			{
				if(OCMFElement.label == 'B')
					startEnergy = OCMFElement.energy;

				if(OCMFElement.label == 'E')
					stopEnergy = OCMFElement.energy;

				cJSON * logArrayElement = cJSON_CreateObject();
				char timeBuffer[50] = {0};
				zntp_format_time(timeBuffer, timestamp);

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
			else
			{
				offlineSession_AppendLogStringWithIntInt("3 Read:CSH CRC FAIL", (int)crcCalc, (int)packetCrc);
			}
		}
	}

	fclose(sessionFile);

	xSemaphoreGive(offs_lock);

	return entryArray;
}

esp_err_t offlineSession_SaveSession(char * sessionData)
{
	int ret = 0;

	if(!offlineSession_is_mounted()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	/// Search for file number to use for this session
	activeFileNumber = offlineSession_FindNewFileNumber();

	offlineSession_AppendLogStringWithInt("1 ActiveFile: ", activeFileNumber);

	if(activeFileNumber < 0)
	{
		return ESP_FAIL;
	}

	sprintf(activePathString,"%s/%d.bin", tmp_path, activeFileNumber);

	//Save the session structure to the file including the start 'B' message
	ret = offlineSession_UpdateSessionOnFile(sessionData, true);

	return ret;
}


void offlineSession_append_energy(char label, time_t timestamp, double energy)
{
	if (!offlineSession_is_mounted()) {
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return;
	}

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
		uint8_t fileVersion = 0;
		fread(&fileVersion, 1, 1, sessionFile);
		ESP_LOGW(TAG, "File version: %d", fileVersion);

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
				if(label == 'T')
					offlineSession_AppendLogString("2 T:nrOfOCMFElements == 0");
				else if(label == 'E')
					offlineSession_AppendLogString("3 E:nrOfOCMFElements == 0");
				else
					offlineSession_AppendLogString("nrOfOCMFElements == 0");

				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %" PRId32 " (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}

			///100 elements, leave room for last element with 'E'
			if((label == 'T') && (nrOfOCMFElements >= (max_offline_signed_values-1)))
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %" PRId32 " (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}

			///Max 100 elements check if room for 'e'
			if((label == 'E') && (nrOfOCMFElements >= max_offline_signed_values))
			{
				ESP_LOGE(TAG, "FileNo %d: Invalid nr of OCMF elements: %" PRId32 " (%c)", activeFileNumber, nrOfOCMFElements, label);
				fclose(sessionFile);
				xSemaphoreGive(offs_lock);
				return;
			}
		}

		ESP_LOGW(TAG, "FileNo %d: &1000: Nr of OCMF elements: %c: %" PRId32 "", activeFileNumber, label, nrOfOCMFElements);

		/// And add 1.

		/// Prepare struct with crc
		struct LogOCMFData line = {.label = label, .energy = energy, .crc = 0};
		offlineSession_SetTimestamp(&line, fileVersion, timestamp);

		uint64_t timestamp = offlineSession_ReadTimestamp(&line, fileVersion);

		uint32_t crc = crc32_normal(0, &line, sizeof(struct LogOCMFData));
		line.crc = crc;

		//ESP_LOGW(TAG, "FileNo %d: writing to OFFS-file with crc=%u", activeFileNumber, line.crc);

		int newElementPosition = (FILE_OCMF_START_ADDR_1004) + (nrOfOCMFElements * sizeof(struct LogOCMFData));
		ESP_LOGW(TAG, "FileNo %d: New element position: #%" PRId32 ": %d", activeFileNumber, nrOfOCMFElements, newElementPosition);
		ESP_LOGW(TAG, "OCMF Write %" PRIi32 " addr: %i : %c %" PRIu64 " %f 0x%" PRIX32 "", nrOfOCMFElements, newElementPosition, line.label, timestamp, line.energy, line.crc);

		/// Write new element
		fseek(sessionFile, newElementPosition, SEEK_SET);
		size_t wrc = fwrite(&line, sizeof(struct LogOCMFData), 1, sessionFile);

		/// Added for diagnostics of empty session
		if(label == 'B')
			offlineSession_AppendLogStringWithInt("1 B: wrc", wrc);

		/// Update nr of elements @1000
		fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);
		nrOfOCMFElements += 1;
		fwrite(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);

		fseek(sessionFile, FILE_NR_OF_OCMF_ADDR_1000, SEEK_SET);

		nrOfOCMFElements = 99;
		fread(&nrOfOCMFElements, sizeof(uint32_t), 1, sessionFile);
		ESP_LOGW(TAG, "FileNo %d: Nr elements: #%" PRId32 "", activeFileNumber, nrOfOCMFElements);
		fclose(sessionFile);
	}

	xSemaphoreGive(offs_lock);
}

void offlineSession_DeleteAllFiles()
{
	ESP_LOGW(TAG, "Deleting all files");
	int fileNo;
	for (fileNo = 0; fileNo < max_offline_session_files; fileNo++)
	{
		offlineSession_delete_session(fileNo);
	}
}

int offlineSession_delete_session(int fileNo)
{
	int ret = 0;

	if(!offlineSession_is_mounted()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	if( xSemaphoreTake( offs_lock, lock_timeout ) != pdTRUE )
	{
		ESP_LOGE(TAG, "failed to obtain offs lock during finalize");
		return -1;
	}

	char buf[32] = {0};
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
