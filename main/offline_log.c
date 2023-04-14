#include "offline_log.h"

#define TAG "OFFLINE_LOG    "

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "errno.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_http_client.h"

#include "certificate.h"
#include "OCMF.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_protocol_serialisation.h"
#include "utz.h"

#include "ocpp_json/ocppj_message_structure.h"
#include "ocpp_json/ocppj_validation.h"

#include "ocpp_task.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"
#include "types/ocpp_diagnostics_status.h"
#include "types/ocpp_date_time.h"

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

enum ocpp_diagnostics_status diagnostics_status = eOCPP_DIAGNOSTICS_STATUS_IDLE;

void send_diagnostics_status_notification(bool is_trigger){
	const char * status = ocpp_diagnostics_status_from_id(diagnostics_status);
	cJSON * status_json = ocpp_create_diagnostics_status_notification_request(status);

	if(status_json == NULL){
		ESP_LOGE(TAG, "Unable to create diagnostics status notification for '%s'", status);
		return;
	}

	int err;
	if(is_trigger){
		err = enqueue_trigger(status_json, NULL, NULL, NULL, eOCPP_CALL_GENERIC, true);
	}else{
		err = enqueue_call_immediate(status_json, NULL, NULL, NULL, eOCPP_CALL_GENERIC);
	}

	if(err != 0){
		ESP_LOGE(TAG, "Unable to send diagnostics status notification for '%s'", status);
		cJSON_Delete(status_json);
	}
}

void update_diagnostics_status(enum ocpp_diagnostics_status new_status){
	if(new_status != diagnostics_status){
		diagnostics_status = new_status;

		if(diagnostics_status != eOCPP_DIAGNOSTICS_STATUS_IDLE){
			send_diagnostics_status_notification(false);
		}

		if(diagnostics_status == eOCPP_DIAGNOSTICS_STATUS_UPLOADED || diagnostics_status == eOCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED){
			diagnostics_status = eOCPP_DIAGNOSTICS_STATUS_IDLE;
		}
	}
}

/**
 * @brief Clamps the log_start/log_end to a time range and returns nr of entries in range or -1 on error.
 */
int find_relevant_entry_range(FILE * fp, time_t from, time_t to, int * log_start, int * log_end){

	if(from == 0 && to == LONG_MAX) // No range specified, use entire log
		return *log_end - *log_start;
	/**
	 * This implementation reads sequencially through the file to find the relevant range, this means that for an upload the
	 * entire range will be read twice and the log_start to relevant_start range will be read once.
	 *
	 * An alternative approach that could be considered would be a binary search using 'seek' to find the the relevant range.
	 * This other approach would mean fewer reads, but may not be more efficient due to random reads and poor caching.
	 *
	 * It is not known if the performance difference will be relevant with the current max_log_items (1000).
	 */

	if(fseek(fp, sizeof(struct LogHeader) + (sizeof(struct LogLine) * (*log_start)), SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to log start");
		return -1;
	}

	struct LogLine line;

	while(*log_start != *log_end){
		if(fread(&line, sizeof(struct LogLine), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read log entry while attemting to find first relevant entry");
			return -1;
		}

		if(line.crc != crc32_normal(0, &line, sizeof(line))){
			ESP_LOGE(TAG, "Mismatch of crc while attempting to find first relevant entry");
			return -1;
		}

		if(line.timestamp > from){
			if(line.timestamp > to)
				return 0; // All values are outside range

			break; // Found first relevant entry
		}

		*log_start = (*log_start +1) % max_log_items;
	}

	if(to < time(NULL)){ // Only update log_end if requested end is before last possible entry

		int current_line = *log_start;

		while(current_line != *log_end){
			if(fread(&line, sizeof(struct LogLine), 1, fp) != 1){
				ESP_LOGE(TAG, "Unable to read log entry while attemting to find first relevant entry");
				return -1;
			}

			if(line.crc != crc32_normal(0, &line, sizeof(line))){
				ESP_LOGE(TAG, "Mismatch of crc while attempting to find first relevant entry");
				return -1;
			}

			if(line.timestamp > to){
				current_line--; // Passed last relevant entry
				break;
			}

			current_line = (current_line+1) % max_log_items;
		}

		*log_end = current_line;
	}

	if(*log_start <= *log_end){
		return *log_end - *log_start;
	}else{
		int entry_count = max_log_items - *log_start;
		entry_count += *log_end;

		return entry_count;
	}
}

#define DIAGNOSTICS_DEFAULT_UPLOAD_RETRIES 1
#define DIAGNOSTICS_DEFAULT_UPLOAD_INTERVAL 180

TimerHandle_t diagnostics_upload_handle = NULL;

struct diagnostics_upload_meta_info{
	char * location; //!< "location (directory) where the diagnostics file shall be uploaded to."
	int retries; //!< "specifies how many times Charge Point must try to upload the diagnostics before giving up"
	int interval; //!< " interval in seconds after which a retry may be attempted."
	time_t log_from; //!< "date and time of the oldest logging information to include in the diagnostics"
	time_t log_to; //!< "date and time of the latest logging information to include in the diagnostic"
};

struct diagnostics_upload_meta_info upload_info = {0};
#define DIAGNOSTICS_ENTRY_SIZE 34

void upload_diagnostics(){

	ESP_LOGI(TAG, "Starting diagnostics upload");

	struct LogLine * log_buffer = NULL;
	FILE * fp = NULL;

	// init_and_lock_log can not be run from a timer, as it may block service task
	//FILE * fp = init_and_lock_log(&log_start, &log_end);

	struct stat st;
	if(stat(tmp_path, &st) != 0){
		ESP_LOGE(TAG, "'%s' not mounted, unable to upload diagnostics", tmp_path);
		goto error;
	}

	if(stat(log_path, &st) != 0){
		ESP_LOGW(TAG, "'%s' no log for diagnostics", tmp_path);
		upload_info.retries = 0; // No need to try again
		goto error;
	}

	// "timer callback function must not [...] specify a non zero block time when accessing [...] a semaphore"
	if(log_lock == NULL || xSemaphoreTake(log_lock, 0) != pdTRUE){
		ESP_LOGE(TAG, "Failed to aquire mutex lock for offline log");
		goto error;
	}

	fp  = fopen(log_path, "rb"); // Seems to not block unlike "wb"
	if(fp==NULL){
		ESP_LOGE(TAG, "Unable to init log");
		goto error;
	}

	struct LogHeader log_header;
	if(fread(&log_header, sizeof(struct LogHeader), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read header");
		goto error;
	}

	int log_start;
	int log_end;

	log_start = log_header.start;
	log_end = log_header.end;

	ESP_LOGI(TAG, "Entries in log file: %d", (log_start <= log_end) ? log_end - log_start : max_log_items - log_start + log_end);
	int entry_count = find_relevant_entry_range(fp, upload_info.log_from, upload_info.log_to, &log_start, &log_end);
	ESP_LOGI(TAG, "Relevant entries:    %d", (log_start <= log_end) ? log_end - log_start : max_log_items - log_start + log_end);

	if(entry_count < 1){
		if(entry_count == 0){
			ESP_LOGE(TAG, "No relevant log entry found");
			upload_info.retries = 0; // No need to try again
		}else{
			ESP_LOGE(TAG, "Error while attempting to find relevant start - stop range");
		}

		goto error;
	}

	size_t free_block_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	size_t required_memory = sizeof(struct LogLine) * entry_count;

	if(free_block_size < required_memory){
		ESP_LOGE(TAG, "Largest external ram block is insufficient %d", free_block_size);
		goto error;
	}else{
		ESP_LOGI(TAG, "Allocating %d from external. Largest block was %d", required_memory, free_block_size);
	}

	log_buffer = heap_caps_malloc(required_memory, MALLOC_CAP_SPIRAM);
	if(log_buffer == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for diagnostics buffer");
		goto error;
	}

	if(fseek(fp, sizeof(struct LogHeader) + (sizeof(struct LogLine) * log_start), SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to relevant log start: %s", strerror(errno));
		goto error;
	}

	bool is_linear = log_end >= log_start;
	size_t read_count = is_linear ? log_end - log_start : max_log_items - log_start;

	if(fread(log_buffer, sizeof(struct LogLine), read_count, fp) != read_count){
		ESP_LOGE(TAG, "Unable to read next entry for upload: %s", strerror(errno));
		goto error;
	}

	if(!is_linear){
		if(fseek(fp, sizeof(struct LogHeader), SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to log entry 0: %s", strerror(errno));
			goto error;
		}

		if(fread(log_buffer + sizeof(struct LogLine) * read_count, sizeof(struct LogLine), log_end, fp) != read_count){
			ESP_LOGE(TAG, "Unable to read next entry for upload: %s", strerror(errno));
			goto error;
		}
	}

	release_log(fp);
	fp = NULL; // in case of error; log_lock should not be re-released

	esp_http_client_config_t config = {
		.url = upload_info.location,
		.method = HTTP_METHOD_POST,
		.use_global_ca_store = certificate_GetUsage(),
		.timeout_ms = 5000
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);

	//Shouldn't this be a standard way to upload? is it just the server that is incomplete?
	/* esp_http_client_set_header(client, "Content-Type", "text/plain"/\*"application/octet-stream"*\/); */
	/* esp_http_client_set_header(client, "Content-Disposition", "attachment; filename=\"diagnostics_log.txt\""); */

	esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=---------------------------405506283132093967342574812273");

	const char * boundry_start = "-----------------------------405506283132093967342574812273\r\n";
	const char * boundry_end = "\r\n-----------------------------405506283132093967342574812273--\r\n";
	const char * body_meta = "Content-Disposition: form-data; filename=\"diagnostics_log.txt\"\r\nContent-Type: text/plain\r\n\r\n";

	int post_len = entry_count * DIAGNOSTICS_ENTRY_SIZE + strlen(boundry_start) + strlen(boundry_end) + strlen(body_meta);

	ESP_LOGI(TAG, "Opening client connection to write %d bytes", post_len);
	esp_err_t err = esp_http_client_open(client, post_len);

	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to open client for upload: %s", esp_err_to_name(err));
		goto error;
	}

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOADING);

	char entry_string[DIAGNOSTICS_ENTRY_SIZE+1];

	char timestamp_str[20];

	if(esp_http_client_write(client, boundry_start, strlen(boundry_start)) != strlen(boundry_start)){
		ESP_LOGE(TAG, "Unable to send bondry for start");
		goto error;
	}
	if(esp_http_client_write(client, body_meta, strlen(body_meta)) != strlen(body_meta)){
		ESP_LOGE(TAG, "Unable to send body metadata");
		goto error;
	}

	for(size_t i = 0; i < entry_count; i++){
		udatetime_t date_time;
		utz_unix_to_datetime(log_buffer[i].timestamp, &date_time);
		utz_datetime_format_iso(timestamp_str, sizeof(timestamp_str), &date_time);

		int written = snprintf(entry_string, DIAGNOSTICS_ENTRY_SIZE+1, "%19s: %e\n", timestamp_str, log_buffer[i].energy);
		if(written != DIAGNOSTICS_ENTRY_SIZE)
			ESP_LOGE(TAG, "Missaligned diagnostics entry result. Width %d, expected %d", written, DIAGNOSTICS_ENTRY_SIZE);

		written = esp_http_client_write(client, entry_string, DIAGNOSTICS_ENTRY_SIZE);
		if(written < DIAGNOSTICS_ENTRY_SIZE){
			ESP_LOGE(TAG, "Unable to write log entry to http client");
			goto error;
		}
	}

	free(log_buffer);
	log_buffer = NULL;

	if(esp_http_client_write(client, boundry_end, strlen(boundry_end)) != strlen(boundry_end)){
		ESP_LOGE(TAG, "Unable to send bondry for end");
		goto error;
	}

	ESP_LOGI(TAG, "Upload complete. Closing connection");
	if(esp_http_client_close(client) != ESP_OK)
		ESP_LOGE(TAG, "Failed to close client");

	if(esp_http_client_cleanup(client) != ESP_OK)
		ESP_LOGE(TAG, "Failed to cleanup client");

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOADED);

	free(upload_info.location);
	upload_info.location = NULL;

	diagnostics_upload_handle = NULL;

	return;
error:
	if(fp != NULL)
		release_log(fp);

	free(log_buffer);

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED);

	if(upload_info.retries > 0){
		upload_info.retries--;
		if(xTimerChangePeriod(diagnostics_upload_handle, pdMS_TO_TICKS(upload_info.interval * 1000), 0) != pdTRUE)
			ESP_LOGE(TAG, "Unable to change period of diagnostics upload");

		if(xTimerReset(diagnostics_upload_handle, 0) != pdTRUE){
			ESP_LOGE(TAG, "Unable to retry diagnostics upload");
			diagnostics_upload_handle = NULL;
		}

	}else{
		diagnostics_upload_handle = NULL;
	}
}

void get_diagnostics_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request for get diagnostics");
	char err_str[124] = {0};

	enum ocppj_err_t err = eOCPPJ_NO_ERROR;

	/**
	 * @todo Consider how to handle multiple simultaneous get diagnostics requests
	 *
	 * The ocpp specification for getDiagnostics.conf does not contain a status or indication that the request will be rejected.
	 * The json spesification does not have a "invalid state" error. This indicates that multiple getDiagnostics.req can be sendt
	 * before file upload is finished. The diagnosticsStatusNotification.req does not have a reference to a getDiagnostics.req
	 * indicating that only one upload can be active.
	 *
	 * Current implementation will only allow one update at a time and use "not supported" error when multiple are requested.
	 *
	 * A failed upload with many retries and high retyInterval may lock the functionality for an unreasonable amount of time
	 * with no way to cancel or reset the request without restarting the device.
	 */
	if(diagnostics_upload_handle != NULL){
		err = eOCPPJ_ERROR_NOT_SUPPORTED;
		sprintf(err_str, "Firmware does not support multiple concurrent diagnostics upload");
		goto error;
	}

	err = ocppj_get_string_field(payload, "location", true, &upload_info.location,
						err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_ERROR)
		goto error;

	// Check that the string contains a valid protocol/scheme
	char * offset = index(upload_info.location, ':');
	if(offset == NULL){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		sprintf(err_str, "'location' does not contain a protocol");
		goto error;
	}

	if(!((offset - upload_info.location == 4 && strncmp(upload_info.location, "http", 4) == 0)
			|| (offset - upload_info.location == 5 && strncmp(upload_info.location, "https", 5)))){

		err = eOCPPJ_ERROR_NOT_SUPPORTED;
		sprintf(err_str, "'location' protocol not supported. Currently only 'http' and 'https' are accepted");
		goto error;
	}
	//TODO: consider adding a maximum limit to url

	// If we accept the location url we need to copy it to allow cJSON_Delete to free the request payload
	upload_info.location = strdup(upload_info.location);

	err = ocppj_get_int_field(payload, "retries", false, &upload_info.retries,
					err_str, sizeof(err_str));

	if(err == eOCPPJ_NO_VALUE){
		upload_info.retries = DIAGNOSTICS_DEFAULT_UPLOAD_RETRIES;
	}else if(err != eOCPPJ_NO_ERROR){
		goto error;
	}

	// TODO: consider also adding max allowed retries (Not specified in ocpp)
	if(upload_info.retries < 1){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		sprintf(err_str, "Expected 'retries' to be a positive integer");
		goto error;
	}

	err = ocppj_get_int_field(payload, "retryInterval", false, &upload_info.interval,
					err_str, sizeof(err_str));

	if(err == eOCPPJ_NO_VALUE){
		upload_info.interval = DIAGNOSTICS_DEFAULT_UPLOAD_INTERVAL;
	}else if(err != eOCPPJ_NO_ERROR){
		goto error;
	}

	// TODO: consider also adding max allowed retry interval (Not specified in ocpp)
	if(upload_info.interval < 1){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		sprintf(err_str, "Expected 'retryInterval' to be a positive integer");
		goto error;
	}

	char * value_str; // temporary value for parsing start and stop time in request

	err = ocppj_get_string_field(payload, "startTime", false, &value_str,
						err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_VALUE){
		if(err != eOCPPJ_NO_ERROR){
			goto error;
		}else{
			upload_info.log_from = ocpp_parse_date_time(value_str);
			if(upload_info.log_from == (time_t)-1){
				err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
				sprintf(err_str, "'startTime' Unrecognised dateTime format");
				goto error;
			}
		}
	}else{
		upload_info.log_from = 0;
	}

	err = ocppj_get_string_field(payload, "stopTime", false, &value_str,
						err_str, sizeof(err_str));

	if(err != eOCPPJ_NO_VALUE){
		if(err != eOCPPJ_NO_ERROR){
			goto error;
		}else{
			upload_info.log_to = ocpp_parse_date_time(value_str);
			if(upload_info.log_to == (time_t)-1){
				err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
				sprintf(err_str, "'stopTime' Unrecognised dateTime format");
				goto error;
			}
		}
	}else{
		upload_info.log_to = LONG_MAX;
	}

	diagnostics_upload_handle = xTimerCreate("upload", pdMS_TO_TICKS(1000), pdFALSE, NULL, upload_diagnostics);
	if(diagnostics_upload_handle == NULL){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "unable to create diagnostics upload timer");
		goto error;
	}

	if(xTimerStart(diagnostics_upload_handle, 0) != pdPASS){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "unable to start diagnostics upload timer");
		goto error;
	}

	cJSON * reply = ocpp_create_get_diagnostics_confirmation(unique_id, NULL);
	if(reply == NULL){
		ESP_LOGE(TAG, "Unable to create get diagnostics confirmation");
	}else{
		send_call_reply(reply);
	}

	return;

error:
	if(err == eOCPPJ_NO_ERROR && err == eOCPPJ_NO_VALUE){
		ESP_LOGE(TAG, "get_diagnostics_cb unexpected termination without known error");
	}else{
		ESP_LOGE(TAG, "get_diagnostics_cb encountered error %d: %s", err, err_str);
	}

	cJSON * error_reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(error_reply == NULL){
		ESP_LOGE(TAG, "Unable to create error reply");
	}else{
		send_call_reply(error_reply);
	}
}

void setup_offline_log(){
	log_lock = xSemaphoreCreateMutex();
	if(log_lock == NULL){
		ESP_LOGE(TAG, "Failed to create mutex for offline log");
		return;
	}
}
