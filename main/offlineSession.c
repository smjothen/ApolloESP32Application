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

static const char *tmp_path = "/offs";
static const char *log_path = "/offs/1.json";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

//static const int max_offline_session_files = 10;

#define FILE_VERSION_ADDR_0  		0
#define FILE_SESSION_ADDR_2  		2
#define FILE_NR_OF_OCMF_ADDR_1000  	1000
#define FILE_OCMF_START_ADDR_1004 	1004

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
unsigned int offlineSession_FindLatestFile()
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
unsigned int offlineSession_FindOldestFile()
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
			break; ///Use the current fileNo
		}

	}

	return fileNo;
}

volatile static int activeFileNumber = -1;
static char activePathString[22] = {0};
static FILE *sessionFile = NULL;



void offlineSession_UpdateSessionOnFile(char *sessionData)
{
	int sessionDataLen = strlen(sessionData)+1;
	char * base64SessionData;// = calloc(500,1);
	size_t outLen = 0;
	base64SessionData = base64_encode(sessionData, sessionDataLen-1, &outLen);
	volatile int base64SessionDataLen = strlen(base64SessionData)+1;

	printf("%d: %s\n", strlen(sessionData), sessionData);
	printf("%d: %s\n", strlen(base64SessionData), base64SessionData);

	//fwrite(sessionData, sessionDataLen, 1, file);
	fseek(sessionFile, FILE_SESSION_ADDR_2, SEEK_SET);
	fwrite(base64SessionData, base64SessionDataLen, 1, sessionFile);

	fclose(sessionFile);

	//memset(sessionData, 0, 1000);
	//memset(base64SessionData, 0, outLen+1);

	free(base64SessionData);
}



esp_err_t offlineSession_PrintFileContent(int fileNo)
{
	char buf[22] = {0};
	sprintf(buf,"/offs/%d.bin", fileNo);
	sessionFile = fopen(buf, "r");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Print: sessionFile == NULL");
		return ESP_FAIL;
	}

	/// Find end of file to get size
	fseek(sessionFile, 0L, SEEK_END);
	size_t readSize = ftell(sessionFile);

	/// Go to beginnning before reading
	fseek(sessionFile, 0L, SEEK_SET);

	char * base64SessionData = calloc(1000, 1);
	volatile size_t size = fread(base64SessionData, 1000, 1, sessionFile);

	fclose(sessionFile);

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
	//printf("%d: %s\n", strlen(sessionDataCreated), sessionDataCreated);
	//sessionDataCreated[outLen] = '\0';
	printf("%d: %.*s\n", strlen(sessionDataCreated), outLen, sessionDataCreated);

	free(base64SessionData);
	free(sessionDataCreated);

	return ESP_OK;
}


esp_err_t offlineSession_SaveSession(char * sessionData)
{
	int ret = 0;

	if(!offlineSession_mount_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	/*FILE *file = fopen("/offs/test.txt", "w");
	//char cont[] = "xcontent";
	int contentLen = strlen(sessionData)+1;
	fwrite(sessionData, strlen(sessionData)+1, 1, file);
	//fflush(file);
	fclose(file);

	memset(sessionData, 0, 1000);

	file = fopen("/offs/test.txt", "r");

	//fseek(file, 2, SEEK_SET);
	//fseek(file, 0L, SEEK_END);
	//long int res = ftell(file);
	fread(sessionData, contentLen, 1, file);
	//printf("%ld: %s\n", res, sessionData);
	printf("%s\n", sessionData);

	fclose(file);*/


	/// Search for file number to use for this session
	activeFileNumber = offlineSession_FindLatestFile();
	if(activeFileNumber < 0)
	{
		return ESP_FAIL;
	}

	//char pathBuf[20] = {0};

	sprintf(activePathString,"/offs/%d.bin", activeFileNumber);

	sessionFile = fopen(activePathString, "wb+");
	//file = fopen("/offs/test.txt", "wb+");

	if(sessionFile == NULL)
	{
		ESP_LOGE(TAG, "Save: sessionFile == NULL");
		return ESP_FAIL;
	}

	offlineSession_UpdateSessionOnFile(sessionData);

	fclose(sessionFile);




	//offlineSession_PrintFileContent();


	offlineSession_append_energy('B', 123, 0.1);

	offlineSession_append_energy('T', 1234, 0.2);

	//offlineSession_append_energy('E', 12345, 0.3);


	offlineSession_FindOldestFile();

	//offlineSession_Print

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
		}
		else
		{
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
		if(nrOfOCMFElements > 0)
			elementOffset = nrOfOCMFElements - 1;

		int newElementPosition = (FILE_OCMF_START_ADDR_1004) + (elementOffset * sizeof(struct LogOCMFData));
		ESP_LOGW(TAG, "FileNo %d: New element position: #%d: %d", activeFileNumber, nrOfOCMFElements, newElementPosition);

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


int offlineSession_delete_sessions()
{
	int ret = 0;

	if(!offlineSession_mount_folder()){
		ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
		return ret;
	}

	FILE *fp = fopen(log_path, "r");
	if(fp==NULL)
		ESP_LOGE(TAG, "Before remove: logfile can't be opened ");
	else
		ESP_LOGE(TAG, "Before remove: logfile can be opened ");

	fclose(fp);

	remove(log_path);

	fp = fopen(log_path, "r");
	if(fp==NULL)
	{
		ESP_LOGE(TAG, "After remove: logfile can't be opened ");
		ret = 1;
	}
	else
	{
		ESP_LOGE(TAG, "After remove: logfile can be opened ");
		ret = 2;
	}

	fclose(fp);

	return ret;
}
