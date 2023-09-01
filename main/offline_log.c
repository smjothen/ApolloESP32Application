#include "offline_log.h"

#define TAG "OFFLINE_LOG    "

#include "esp_log.h"
#include "errno.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#include "OCMF.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_protocol_serialisation.h"

static const char *tmp_path = "/files";
static const char *log_path = "/files/log554.bin";

static SemaphoreHandle_t log_lock = NULL;

static const int max_log_items = 1000;
static bool disabledForCalibration = false;

struct LogHeader {
    int start;
    int end;
    uint32_t crc; // for header, not whole file
    // dont keep version, use other file name for versioning
};


struct LogLine {
    int timestamp;
    double energy;
    uint32_t crc;
};

void offlineLog_disable(void) {
    disabledForCalibration = true;
}

int update_header(FILE *fp, int start, int end){
    struct LogHeader new_header = {.start=start, .end=end, .crc=0};
    uint32_t crc =  crc32_normal(0, &new_header, sizeof(new_header));
    new_header.crc = crc;
    fseek(fp, 0, SEEK_SET);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    int write_result = fwrite(&new_header, 1,  sizeof(new_header), fp);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    ESP_LOGI(TAG, "writing header %d %d %u (s:%d, res:%d)    <<<<   ", 
        new_header.start, new_header.end, new_header.crc, sizeof(new_header), write_result
    );

    if(write_result!=sizeof(new_header)){
        return -1;
    }

    return 0;
}

int ensure_valid_header(FILE *fp, int *start_out, int *end_out){
    struct LogHeader head_in_file = {0};
    fseek(fp, 0, SEEK_SET);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    int read_result = fread(&head_in_file, 1,sizeof(head_in_file),  fp);
    ESP_LOGI(TAG, "header on disk %d %d %u (s:%d, res:%d)    <<<   ", 
    head_in_file.start, head_in_file.end, head_in_file.crc, sizeof(head_in_file), read_result);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    if (read_result < sizeof (head_in_file)) {
        ESP_LOGE(TAG, "fread errno %d: %s", errno, strerror(errno));
    }
    uint32_t crc_in_file = head_in_file.crc;
    head_in_file.crc = 0;

    uint32_t calculated_crc = crc32_normal(0, &head_in_file, sizeof(head_in_file));

    if(crc_in_file == calculated_crc){
        ESP_LOGI(TAG, "Found valid header");
        *start_out = head_in_file.start;
        *end_out = head_in_file.end;
    }else{
        ESP_LOGE(TAG, "INVALID HEAD, staring log anew");

        int new_header_result = update_header(fp, 0, 0);
        *start_out = 0;
        *end_out = 0;

        if(new_header_result<0){
            return -1;
        }
    }

    return 0;

}

FILE * init_and_lock_log(int *start, int *end){

  if (disabledForCalibration) {
    ESP_LOGI(TAG, "Blocking offline log during calibration!");
    return NULL;
  }

	struct stat st;
	if(stat(tmp_path, &st) != 0){
		ESP_LOGE(TAG, "'%s' not mounted, offline log will not work", tmp_path);
		return NULL;
	}

  if (log_lock == NULL) {
      ESP_LOGE(TAG, "No allocated mutex for offline log!");
      return NULL;
  }

	if (xSemaphoreTake(log_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to aquire mutex lock for offline log");
		return NULL;
	}

  FILE *fp = NULL;
  if (stat(log_path, &st) != 0){
      // Doesn't exist, w+ will create
      fp = fopen(log_path, "w+b");
  }else{
      // Does exist, r+ will allow updating
      fp = fopen(log_path, "r+b");
  }

    if(fp==NULL){
        ESP_LOGE(TAG, "failed to open file ");
        xSemaphoreGive(log_lock);
        return NULL;
    }

    size_t log_size = sizeof(struct LogHeader) + (sizeof(struct LogLine)*max_log_items);

    int seek_res = fseek(
        fp,
        log_size, 
        SEEK_SET
    );

    if(seek_res < 0){
        xSemaphoreGive(log_lock);
        return NULL;
    }
    ESP_LOGI(TAG, "expanded log to %d(%d)", log_size, seek_res);

    fseek(fp, 0, SEEK_SET);

    if(ensure_valid_header(fp, start, end)<0){
        ESP_LOGE(TAG, "failed to create log header");
        xSemaphoreGive(log_lock);
        return NULL;
    }

    return fp;
}

int release_log(FILE * fp){
	int result = 0;
	if(fp != NULL && fclose(fp) != 0){
		result = EOF;
		ESP_LOGE(TAG, "Unable to close offline log: %s", strerror(errno));
	}

	if(xSemaphoreGive(log_lock) != pdTRUE) // "indicating that the semaphore was not first obtained correctly"
		ESP_LOGE(TAG, "Unable to give log mutex");

	return result;
}

void append_offline_energy(int timestamp, double energy){
    ESP_LOGI(TAG, "saving offline energy %fWh@%d", energy, timestamp);

    int log_start;
    int log_end;
    FILE *fp = init_and_lock_log(&log_start, &log_end);

    if(fp==NULL) {
        return;
    }

    int new_log_end = (log_end+1) % max_log_items;
    if(new_log_end == log_start){
        // insertion would fill the buffer, and cant tell if it is full or empty
        // move first item idx to compensate
        log_start = (log_start+1) % max_log_items;
    }

    struct LogLine line = {.energy = energy, .timestamp = timestamp, .crc = 0};

    uint32_t crc = crc32_normal(0, &line, sizeof(struct LogLine));
    line.crc = crc;

    ESP_LOGI(TAG, "writing to file with crc=%u", line.crc);

    int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_end);
    fseek(fp, start_of_line, SEEK_SET);
    int bytes_written = fwrite(&line, 1, sizeof(line), fp);

    update_header(fp, log_start, new_log_end);
    ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
        bytes_written, start_of_line, log_start, new_log_end
    );

    release_log(fp);
}

int attempt_log_send(void){
    ESP_LOGI(TAG, "log data:");

    int result = -1;
    int log_start;
    int log_end;
    FILE *fp = init_and_lock_log(&log_start, &log_end);

    //If file or partition is now available, indicate empty file
    if(fp == NULL)
    	return 0;

    char ocmf_text[200] = {0};

    while(log_start!=log_end){
        struct LogLine line;
        int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_start);
        fseek(fp, start_of_line, SEEK_SET);
        int read_result = fread(&line, 1,sizeof(line),  fp);

        uint32_t crc_on_file = line.crc;
        line.crc = 0;
        uint32_t calculated_crc = crc32_normal(0, &line, sizeof(line));


        ESP_LOGI(TAG, "LogLine@%d>%d: E=%f, t=%d, crc=%d, valid=%d, read=%d", 
            log_start, start_of_line,
            line.energy, line.timestamp,
            crc_on_file, crc_on_file==calculated_crc, read_result
        );

        if(crc_on_file==calculated_crc){
            OCMF_SignedMeterValue_CreateMessageFromLog(ocmf_text, line.timestamp, line.energy);
            int publish_result = publish_string_observation_blocked(
			    SignedMeterValue, ocmf_text, 2000
		    );

            if(publish_result<0){
                ESP_LOGI(TAG, "publishing line failed, aborting log dump");
                break;
            }

            int new_log_start = (log_start + 1) % max_log_items;
            update_header(fp, new_log_start, log_end);
            fflush(fp);
            

            ESP_LOGI(TAG, "line published");


        }else{
            ESP_LOGI(TAG, "skipped corrupt line");
        }

        log_start = (log_start+1) % max_log_items;        
    }

    if(log_start==log_end){
        result = 0;
        if(log_start!=0)
            update_header(fp, 0, 0);
    }

    release_log(fp);
    return result;
}


int deleteOfflineLog()
{
	struct stat st;
	if(stat(tmp_path, &st) != 0){
		ESP_LOGE(TAG, "'%s' not mounted. Unable to delete offline log", tmp_path);
		return ENOTDIR;
	}

	if(log_lock == NULL){
		return ENOLCK;
	}

	if(xSemaphoreTake(log_lock, pdMS_TO_TICKS(5000)) != pdTRUE){
		ESP_LOGE(TAG, "Failed to aquire mutex lock to delete offline log");
		return ETIMEDOUT;
	}

	int ret = remove(log_path);
	if(ret != 0){
		ret = errno;
		ESP_LOGE(TAG, "Failed to remove offline log: %s", strerror(ret));
	}

	xSemaphoreGive(log_lock);

	return ret;
}

void setup_offline_log(){
	log_lock = xSemaphoreCreateMutex();
	if(log_lock == NULL){
		ESP_LOGE(TAG, "Failed to create mutex for offline log");
		return;
	}
}
