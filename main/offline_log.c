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

static SemaphoreHandle_t log_lock = NULL;

static const int max_log_items = 1000;
static bool disabledForCalibration = false;

#define OFFLINE_LOG_FLAG_64BIT (1 << 0)

struct LogFile {
    const char *path;
    uint32_t flags;
    size_t entry_size;
};

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

struct LogLine64 {
    time_t timestamp;
    double energy;
    uint32_t crc;
};

static const struct LogFile log32 = {
    .path = "/files/log554.bin",
    .flags = 0,
    .entry_size = sizeof (struct LogLine),
};

// If sizeof (time_t) == 64 this will be used for logging, while the file above will be
// only sent to the cloud and cleared.
static const struct LogFile log64 = {
    .path = "/files/log64.bin",
    .flags = OFFLINE_LOG_FLAG_64BIT,
    .entry_size = sizeof (struct LogLine64),
};

int update_header(FILE *fp, int start, int end){
    struct LogHeader new_header = {.start=start, .end=end, .crc=0};
    uint32_t crc =  crc32_normal(0, &new_header, sizeof(new_header));
    new_header.crc = crc;
    fseek(fp, 0, SEEK_SET);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    int write_result = fwrite(&new_header, 1,  sizeof(new_header), fp);
    ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
    ESP_LOGI(TAG, "writing header %d %d %" PRIu32 " (s:%d, res:%d)    <<<<   ",
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
    ESP_LOGI(TAG, "header on disk %d %d %" PRIu32 " (s:%d, res:%d)    <<<   ",
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

FILE * init_and_lock_log(const struct LogFile *file, int *start, int *end){

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
  if (stat(file->path, &st) != 0){
      // Doesn't exist, w+ will create
      fp = fopen(file->path, "w+b");
  }else{
      // Does exist, r+ will allow updating
      fp = fopen(file->path, "r+b");
  }

    if(fp==NULL){
        ESP_LOGE(TAG, "failed to open file ");
        xSemaphoreGive(log_lock);
        return NULL;
    }

    size_t log_size = sizeof (struct LogHeader) + file->entry_size * max_log_items;

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

#ifdef OFFLINE_LOG_LEGACY_LOGGING

void offline_log_append_energy_legacy(time_t timestamp, double energy){
    ESP_LOGI(TAG, "saving offline energy %fWh@%lld", energy, timestamp);

    int log_start;
    int log_end;

    FILE *fp = init_and_lock_log(&log32, &log_start, &log_end);

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

    uint32_t crc = crc32_normal(0, &line, sizeof(line));
    line.crc = crc;

    ESP_LOGI(TAG, "writing to file with crc=%" PRIu32 "", line.crc);

    int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_end);
    fseek(fp, start_of_line, SEEK_SET);
    int bytes_written = fwrite(&line, 1, sizeof(line), fp);

    update_header(fp, log_start, new_log_end);
    ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
        bytes_written, start_of_line, log_start, new_log_end
    );

    int close_result = fclose(fp);
    ESP_LOGI(TAG, "closed log file %d", close_result);
}

#endif

void offline_log_append_energy(time_t timestamp, double energy){
    ESP_LOGI(TAG, "saving offline energy %fWh@%lld", energy, timestamp);

    int log_start;
    int log_end;

    // Always append to 64bit path for future energy
    FILE *fp = init_and_lock_log(&log64, &log_start, &log_end);

    if(fp==NULL)
        return;

    int new_log_end = (log_end+1) % max_log_items;
    if(new_log_end == log_start){
        // insertion would fill the buffer, and cant tell if it is full or empty
        // move first item idx to compensate
        log_start = (log_start+1) % max_log_items;
    }

    struct LogLine64 line = {.energy = energy, .timestamp = timestamp, .crc = 0};

    uint32_t crc = crc32_normal(0, &line, sizeof(line));
    line.crc = crc;

    ESP_LOGI(TAG, "writing to file with crc=%" PRIu32 "", line.crc);

    int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_end);
    fseek(fp, start_of_line, SEEK_SET);
    int bytes_written = fwrite(&line, 1, sizeof(line), fp);

    update_header(fp, log_start, new_log_end);
    ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
        bytes_written, start_of_line, log_start, new_log_end
    );

    release_log(fp);
}

int _offline_log_read_line(FILE *fp, int index, int *line_start, double *line_energy,
        time_t *line_ts, uint32_t *line_crc, uint32_t *calc_crc) {

    struct LogLine line;
    *line_start = sizeof(struct LogHeader) + (sizeof(line) * index);

    fseek(fp, *line_start, SEEK_SET);
    int res = fread(&line, 1, sizeof(line),  fp);

    *line_energy = line.energy;
    *line_ts = line.timestamp;
    *line_crc = line.crc;

    line.crc = 0;
    *calc_crc = crc32_normal(0, &line, sizeof(line));

    return res;
}

int _offline_log_read_line64(FILE *fp, int index, int *line_start, double *line_energy,
        time_t *line_ts, uint32_t *line_crc, uint32_t *calc_crc) {

    struct LogLine64 line;
    *line_start = sizeof(struct LogHeader) + (sizeof(line) * index);

    fseek(fp, *line_start, SEEK_SET);
    int res = fread(&line, 1, sizeof(line),  fp);

    *line_energy = line.energy;
    *line_ts = line.timestamp;
    *line_crc = line.crc;

    line.crc = 0;
    *calc_crc = crc32_normal(0, &line, sizeof(line));

    return res;
}

int _offline_log_attempt_send(const struct LogFile *file) {
    ESP_LOGI(TAG, "log data (%s):", file->path);

    int result = -1;
    int log_start;
    int log_end;
    FILE *fp = init_and_lock_log(file, &log_start, &log_end);

    //If file or partition is now available, indicate empty file
    if(fp == NULL)
    	return 0;

    char ocmf_text[200] = {0};

    while(log_start!=log_end){
        int read_result, start_of_line;
        uint32_t crc_on_file, calculated_crc;
        double energy;
        time_t timestamp;

        if (file->flags & OFFLINE_LOG_FLAG_64BIT) {
            read_result = _offline_log_read_line64(fp, log_start, &start_of_line, &energy, &timestamp, &crc_on_file, &calculated_crc);
        } else {
            read_result = _offline_log_read_line(fp, log_start, &start_of_line, &energy, &timestamp, &crc_on_file, &calculated_crc);
        }

        ESP_LOGI(TAG, "LogLine%d@%d>%d: E=%f, t=%lld, crc=%" PRId32 ", valid=%d, read=%d",
            (file->flags & OFFLINE_LOG_FLAG_64BIT) ? 64 : 32,
            log_start, start_of_line,
            energy, timestamp,
            crc_on_file, crc_on_file==calculated_crc, read_result
        );

        if(crc_on_file==calculated_crc){
            OCMF_SignedMeterValue_CreateMessageFromLog(ocmf_text, timestamp, energy);
            int publish_result = publish_string_observation_blocked(SignedMeterValue, ocmf_text, 2000);

            if(publish_result<0){
                ESP_LOGI(TAG, "publishing line failed, aborting log dump");
                break;
            }

            int new_log_start = (log_start + 1) % max_log_items;
            update_header(fp, new_log_start, log_end);
            fflush(fp);

            ESP_LOGI(TAG, "line published");
        } else {
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

int offline_log_attempt_send(void) {
    // If old 32 bit file exists, attempt to send it and then remove the file if successful
    if (access(log32.path, R_OK) == 0) {
        ESP_LOGI(TAG, "Found legacy log file %s, attempting send", log32.path);

        int ret = _offline_log_attempt_send(&log32);
        if (ret != 0) {
            ESP_LOGE(TAG, "Attempted send of legacy log failed!");
            return ret;
        }

        ret = remove(log32.path);
        ESP_LOGI(TAG, "Deleting legacy log %s = %d", log32.path, ret);
    }

    // Always attempt to send 64-bit log
    int ret = _offline_log_attempt_send(&log64);
    if (ret != 0) {
        ESP_LOGE(TAG, "Attempted send of log failed!");
        return ret;
    }

    ESP_LOGI(TAG, "Attempted send of log successful!");
    return ret;
}

int offline_log_delete(void)
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

  const struct LogFile files[] = { log32, log64 };

  int ret = 0;

  for (size_t i = 0; i < sizeof (files) / sizeof (files[0]); i++) {
      const struct LogFile *file = &files[i];

      FILE *fp = fopen(file->path, "r");

      if(fp==NULL) {
        ESP_LOGE(TAG, "Before remove: logfile %s can't be opened ", file->path);
      } else {
        ESP_LOGE(TAG, "Before remove: logfile %s can be opened ", file->path);
        fclose(fp);
      }

      remove(file->path);

      fp = fopen(file->path, "r");
      if(fp==NULL)
      {
        ESP_LOGE(TAG, "After remove: logfile %s can't be opened ", file->path);
        ret = 1;
      }
      else
      {
        ESP_LOGE(TAG, "After remove: logfile %s can be opened ", file->path);
        fclose(fp);
        ret = 2;
      }
  }

	return ret;
}

void setup_offline_log(){
	log_lock = xSemaphoreCreateMutex();
	if(log_lock == NULL){
		ESP_LOGE(TAG, "Failed to create mutex for offline log");
		return;
	}
}
