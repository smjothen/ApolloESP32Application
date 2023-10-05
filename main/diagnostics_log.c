#include "diagnostics_log.h"
#ifdef CONFIG_ZAPTEC_DIAGNOSTICS_LOG

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_http_client.h"
#include "esp_crc.h"

#include "diagnostics_log.h"
#include "certificate.h"
#include "zaptec_cloud_observations.h"
#include "types/ocpp_date_time.h"
#include "rfc3986.h"

#include "ocpp_json/ocppj_message_structure.h"
#include "ocpp_json/ocppj_validation.h"

#include "ocpp_task.h"
#include "messages/call_messages/ocpp_call_request.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"

#include "types/ocpp_diagnostics_status.h"
#include "types/ocpp_date_time.h"

static const char * file_path = CONFIG_ZAPTEC_DIAGNOSTICS_LOG_MOUNT_POINT "/dia_log.bin";
static SemaphoreHandle_t file_lock = NULL;

struct log_header{
	uint8_t version;
	long write_offset; // or next entry offset
	long read_offset; // or oldest entry offset
	bool wrapped;
};

struct log_entry_info{
	time_t timestamp;
	size_t length;
	esp_log_level_t severity;
};

#define OFFSET_HEADER 0
#define OFFSET_CONTENT OFFSET_HEADER + sizeof(struct log_header) + sizeof(uint32_t)

FILE * diagnostics_file = NULL;
time_t last_sync = 0;

struct log_header header;
bool is_header_valid = false;

static char * entry_content = NULL;
#define DIAGNOSTICS_LOG_ENTRY_CONTENT_OUT_SIZE CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE + 64

static bool filesystem_is_ready(){
	return (file_lock != NULL && diagnostics_file != NULL);
}

static esp_err_t write_header(FILE * fp, const struct log_header * header){
	if(fseek(fp, OFFSET_HEADER, SEEK_SET) != 0){
		printf("Unable seek to write header: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(header, sizeof(struct log_header), 1, fp) != 1){
		printf("Unable to write header: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc = esp_crc32_le(0, (uint8_t *)header, sizeof(struct log_header));
	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		printf("Unable to write header crc: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	return ESP_OK;
}

static esp_err_t read_header(FILE * fp, struct log_header * header){
	if(fseek(fp, OFFSET_HEADER, SEEK_SET) != 0){
		printf("Unable to seek to read header: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	if(fread(header, sizeof(struct log_header), 1, fp) != 1){

		if(feof(diagnostics_file) != 0){
			printf("No diagnostics log header written\n");
			return ESP_ERR_NOT_FOUND;
		}else{
			printf("Unable to read header: %s\n", strerror(errno));
			return ESP_FAIL;
		}
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		printf("Unable to read header crc: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	if(crc != esp_crc32_le(0, (uint8_t *)header, sizeof(struct log_header))){
		printf("Crc mismatch for header: %s\n", strerror(errno));
		return ESP_ERR_INVALID_CRC;
	}

	return ESP_OK;
}

static esp_err_t update_header_if_required(){

	if(is_header_valid){
		return ESP_OK;
	}else{
		esp_err_t err = read_header(diagnostics_file, &header);
		is_header_valid = (err == ESP_OK);
		return err;
	}
}

struct entry_crc_buffer{
	uint32_t crc_info;
	uint32_t crc_content;
};

//NOTE: entry_content_out must be large enough to contain the maximum stored entry length + length of date
static esp_err_t read_entry(FILE * fp, struct log_entry_info * entry_info_out, char * entry_content_out){
	if(fread(entry_info_out, sizeof(struct log_entry_info), 1, fp) != 1){
		if(ferror(fp) == 0){
			printf("No entry info found\n");
			return ESP_ERR_NOT_FOUND;
		}else{
			printf("Failed to read entry info\n");
			return ESP_FAIL;
		}
	}

	if(entry_info_out->length > CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE){
		printf("Invalid length in entry_info\n");
		return ESP_ERR_INVALID_SIZE;
	}

	int date_length = ocpp_print_date_time(entry_info_out->timestamp, entry_content_out, 30);
	if(date_length > 30)
		date_length = 30;

	entry_content_out[date_length++] = ' ';

	if(fread(entry_content_out + date_length, entry_info_out->length, 1, fp) != 1){
		printf("Unable to read entry content %ld, length %u\n", ftell(fp), entry_info_out->length);
		return ESP_FAIL;
	}

	uint32_t crc_written;
	if(fread(&crc_written, sizeof(uint32_t), 1, fp) != 1){
		printf("Unable to read entry crc\n");
		return ESP_FAIL;
	}

	struct entry_crc_buffer crc_buffer = {
		.crc_info = esp_crc32_le(0, (uint8_t *)entry_info_out, sizeof(struct log_entry_info)),
		.crc_content = esp_crc32_le(0, (uint8_t *)(entry_content_out + date_length), entry_info_out->length)
	};

	if(crc_written != esp_crc32_le(0, (uint8_t *)&crc_buffer, sizeof(struct entry_crc_buffer))){
		printf("CRC did not match for entry\n");
		return ESP_ERR_INVALID_CRC;
	}

	entry_content_out[entry_info_out->length += date_length] = '\0';
	return ESP_OK;
}

esp_log_level_t esp_log_level_from_char(char severity_char){
	switch(severity_char){
	case 'E':
	        return ESP_LOG_ERROR;
	case 'W':
		return ESP_LOG_WARN;
	case 'I':
		return ESP_LOG_INFO;
	case 'D':
		return ESP_LOG_DEBUG;
	case 'V':
		return ESP_LOG_VERBOSE;
	default:
		return ESP_LOG_INFO; // Logging from binary blobs will be seen as info
	}
}

char char_from_esp_log_level(esp_log_level_t severity){
	switch(severity){
	case ESP_LOG_ERROR:
		return 'E';
	case ESP_LOG_WARN:
		return 'W';
	case ESP_LOG_INFO:
		return 'I';
	case ESP_LOG_DEBUG:
		return 'D';
	case ESP_LOG_VERBOSE:
		return 'V';
	default:
		return 'I'; // Logging from binary blobs will be seen as info
	}
}

static esp_err_t write_entry(FILE * fp, struct log_entry_info * entry_info, const char * entry, bool * should_truncate_out, off_t * truncate_length_out){

#ifdef CONFIG_LOG_COLORS
	/*
	 * Attempt to skip the color info. Example sequence:
	 *  \033[0;32mI (09:15:20.321) MAIN memory free: 9132, min: 4824, largest block: 8960\033[0m\n
	 */
	if(strncmp(entry, "\033[", 2) == 0){
		size_t color_end = entry_info->length > 6 ? 6 : entry_info->length;
		for(size_t i = 3; i <= color_end; i++){
			if(entry[i] == 'm'){
				entry = entry+i+1;
				entry_info->length -= i+1;

				if(strncmp(entry + entry_info->length - 5, "\033[0m", 4) == 0){
					entry_info->length -= 5;
				}
				break;
			}
		}
	}
#endif /*CONFIG_LOG_COLORS*/

	entry_info->severity = esp_log_level_from_char(entry[0]);

	if(entry_info->severity > CONFIG_ZAPTEC_DIAGNOSTICS_LOG_LEVEL)
		return ESP_OK;

	/*
	 * Attempt to remove date. example sequence:
	 *  I (09:15:20.321) MAIN memory free: 9132, min: 4824, largest block: 8960
	 * and:
	 *  I (198474) wifi:state: run -> init (6c0)
	 */
	if(entry_info->length > 19 && entry[2] == '('){
		for(size_t i = 4; i < 17; i++){
			if(entry[i] == ')'){
				entry = entry + i + 2;
				entry_info->length -= i+2;
				break;
			}
		}
	}

	if(entry_info->length > CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE)
		entry_info->length = CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE;

	size_t complete_entry_size = sizeof(struct log_entry_info) + entry_info->length + sizeof(uint32_t);

	esp_err_t header_result = update_header_if_required();
	if(header_result != ESP_OK)
		return header_result;

	if(header.write_offset + complete_entry_size > CONFIG_ZAPTEC_DIAGNOSTICS_LOG_SIZE){
		*should_truncate_out = true;
		*truncate_length_out = header.write_offset; // truncate_length may later be updated if new entry writes past this point

		header.wrapped = true;
		header.write_offset = OFFSET_CONTENT;
		if(header.read_offset >= *truncate_length_out)
			header.read_offset = OFFSET_CONTENT;
	}

	if(header.wrapped && complete_entry_size > header.read_offset - header.write_offset){
		if(fseek(fp, header.read_offset, SEEK_SET) != 0){
			printf("Unable to seek to read offset: %s\n", strerror(errno));
			return ESP_FAIL;
		}
		struct log_entry_info old_info;

		if(entry_content == NULL){
			entry_content = malloc(DIAGNOSTICS_LOG_ENTRY_CONTENT_OUT_SIZE);

			if(entry_content == NULL)
				return ESP_ERR_NO_MEM;
		}

		long current_offset = header.read_offset;

		while(current_offset < header.write_offset + complete_entry_size){
			esp_err_t read_result = read_entry(fp, &old_info, entry_content);

			if(read_result == ESP_OK){
				current_offset = ftell(fp);

			} else {// Overwriting last or  entry is invalid. If invalid the offset for remaining entries can not be trusted
				printf("Write special case: %s\n", esp_err_to_name(read_result));
				*should_truncate_out = true;
				*truncate_length_out = header.write_offset;

				if(header.read_offset >= *truncate_length_out){
					header.wrapped = false; // TODO: Check if it should be wrapped
					current_offset = OFFSET_CONTENT; // read to end and start of next cycle.
				}
				break;
			}
		}

		header.read_offset = current_offset;
	}

	if(fseek(fp, header.write_offset, SEEK_SET) != 0){
		printf("Unable to seek to write entry: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(entry_info, sizeof(struct log_entry_info), 1, fp) != 1){
		printf("Unable to write entry info: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(entry, entry_info->length, 1, fp) != 1){
		printf("Unable to write entry: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	struct entry_crc_buffer crc_buffer = {
		.crc_info = esp_crc32_le(0, (uint8_t *)entry_info, sizeof(struct log_entry_info)),
		.crc_content = esp_crc32_le(0, (uint8_t *)entry, entry_info->length)
	};

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&crc_buffer, sizeof(struct entry_crc_buffer));
	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		printf("Unable to write entry crc: %s\n", strerror(errno));
		return ESP_FAIL;
	}

	header.write_offset = ftell(fp);
	if(*should_truncate_out && header.write_offset > *truncate_length_out){
		*truncate_length_out = header.write_offset;
	}

	header_result = write_header(fp, &header);

	if(header_result != ESP_OK)
		is_header_valid = false;

	return header_result;
}

char * log_buffer = NULL;

esp_err_t diagnostics_log_empty(){

	esp_err_t err = ESP_FAIL;

	if(!filesystem_is_ready())
		return ESP_ERR_INVALID_STATE;

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2500)) != pdTRUE)
		return ESP_ERR_TIMEOUT;

	if(fclose(diagnostics_file)){ // If the call fails (bad file), then we will abandon the FILE *
		printf("Unable to close file: %s\n", strerror(errno));
	}

	diagnostics_file = fopen(file_path, "wb+");
	if(diagnostics_file == NULL){
		printf("Unable to open file: %s\n", strerror(errno));
		goto error;
	}

	header.version = 1;
	header.write_offset = OFFSET_CONTENT;
	header.read_offset = OFFSET_CONTENT;
	header.wrapped = false;

	err = write_header(diagnostics_file, &header);
error:
	if(err != ESP_OK)
		is_header_valid = false;
	xSemaphoreGive(file_lock);

	return err;
}

esp_err_t diagnostics_log_write(const char * fmt, va_list ap){

	if(log_buffer == NULL){
		log_buffer = malloc(CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE);

		if(log_buffer == NULL){
			return ESP_ERR_NO_MEM;
		}
	}

	int written = vsnprintf(log_buffer, CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE, fmt, ap);
	if(written >= CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE){
		written = CONFIG_ZAPTEC_DIAGNOSTICS_LOG_ENTRY_SIZE-1;

	}else if(written < 0){
		return ESP_FAIL;
	}

	struct log_entry_info entry_info = {
		.timestamp = time(NULL),
		.length = written
	};

	bool should_truncate = false;
	off_t truncate_length;
	esp_err_t write_result = write_entry(diagnostics_file, &entry_info, log_buffer, &should_truncate, &truncate_length);

	if(last_sync + 60 < time(NULL) || should_truncate){
		// We close the file routinely to make sure it is saved in case of reboot.
		// An alternative would be to use fflush which works on the filedes and not of FILE *.

		if(!should_truncate){
			if(fflush(diagnostics_file) != 0){
				printf("Failed to fflush file: %s\n", strerror(errno));
			}
			fsync(fileno(diagnostics_file)); // https://github.com/espressif/esp-idf/issues/2820

		}else{
			if(fclose(diagnostics_file) != 0)
				printf("Failed to close file: %s\n", strerror(errno));

			if(truncate(file_path, truncate_length) != 0)
				printf("Failed to truncate file: %s\n", strerror(errno));

			// We reopen the file regardless of wheter or not close succeeded as if it fails the FILE * can not be trusted
			diagnostics_file = fopen(file_path, "rb+");
		}

		last_sync = time(NULL);
	}

	return write_result;
}

cloud_event_level esp_log_level_to_event_level(esp_err_t  level){
	if(level == ESP_LOG_ERROR){
		return cloud_event_level_error;
	}else if(level == ESP_LOG_WARN){
		return cloud_event_level_warning;
	}else{
		return cloud_event_level_information;
	}
}

// TODO: Consider not calling this from cloud_listener thread to allow cloud_listener to handle mqtt publish response (are there duplicate events due to missing ack?)
esp_err_t diagnostics_log_publish_as_event(){
	if(!filesystem_is_ready()){
		return ESP_ERR_INVALID_STATE;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(500)) != pdTRUE){
		return ESP_ERR_TIMEOUT;
	}

	esp_err_t result = update_header_if_required();
	if(result != ESP_OK)
		goto error;

	struct log_entry_info info;

	if(entry_content == NULL){
		entry_content = malloc(DIAGNOSTICS_LOG_ENTRY_CONTENT_OUT_SIZE);

		if(entry_content == NULL){
			result = ESP_ERR_NO_MEM;
			goto error;
		}
	}

	if(fseek(diagnostics_file, header.read_offset, SEEK_SET) != 0){
		result = ESP_FAIL;
	}else{
		while(result = read_entry(diagnostics_file, &info, entry_content), result == ESP_OK)
			publish_debug_message_event(entry_content, esp_log_level_to_event_level(info.severity));
	}

	if(result == ESP_ERR_NOT_FOUND)
		result = ESP_OK;

	if(result == ESP_OK && header.wrapped){
		if(fseek(diagnostics_file, OFFSET_CONTENT, SEEK_SET) != 0){
			result = ESP_FAIL;
		}else{
			while(ftell(diagnostics_file) < header.write_offset
				&& (result = read_entry(diagnostics_file, &info, entry_content), result == ESP_OK)){
				publish_debug_message_event(entry_content, esp_log_level_to_event_level(info.severity));
			}
		}
	}
error:
	xSemaphoreGive(file_lock);
	return result;
}

enum ocpp_diagnostics_status diagnostics_status = eOCPP_DIAGNOSTICS_STATUS_IDLE;

void send_diagnostics_status_notification(bool is_trigger){
	const char * status = ocpp_diagnostics_status_from_id(diagnostics_status);
	cJSON * status_json = ocpp_create_diagnostics_status_notification_request(status);

	if(status_json == NULL){
		printf("Unable to create diagnostics status notification for '%s'\n", status);
		return;
	}

	int err;
	if(is_trigger){
		err = enqueue_trigger(status_json, NULL, NULL, NULL, eOCPP_CALL_GENERIC, true);
	}else{
		err = enqueue_call_immediate(status_json, NULL, NULL, NULL, eOCPP_CALL_GENERIC);
	}

	if(err != 0){
		printf("Unable to send diagnostics status notification for '%s'\n", status);
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

#define DIAGNOSTICS_DEFAULT_UPLOAD_RETRIES 1
#define DIAGNOSTICS_DEFAULT_UPLOAD_INTERVAL 60

TimerHandle_t diagnostics_upload_handle = NULL;

struct diagnostics_upload_meta_info{
	char * location; //!< "location (directory) where the diagnostics file shall be uploaded to."
	int retries; //!< "specifies how many times Charge Point must try to upload the diagnostics before giving up"
	int interval; //!< " interval in seconds after which a retry may be attempted."
	time_t log_from; //!< "date and time of the oldest logging information to include in the diagnostics"
	time_t log_to; //!< "date and time of the latest logging information to include in the diagnostic"
};

struct diagnostics_upload_meta_info upload_info = {0};

void upload_diagnostics_ocpp(){

	char * upload = NULL;
	bool release_lock = false;
	esp_err_t result = ESP_FAIL;

	if(!filesystem_is_ready()){
		printf("Filesyste Not ready for diagnostics upload\n");
		goto error;
	}

	// "timer callback function must not [...] specify a non zero block time when accessing [...] a semaphore"
	if(xSemaphoreTake(file_lock, 0) != pdTRUE){
		printf("Unable to take semaphore for diagnostics upload\n");
		goto error;
	}

	release_lock = true;

	if(update_header_if_required() != ESP_OK)
		goto error;

	if(fseek(diagnostics_file, header.read_offset, SEEK_SET) != 0){
		printf("Unable to seek to read offset for diagnostics upload");
		goto error;
	}
	long offset_from;

	esp_http_client_config_t config = {
		.url = upload_info.location,
		.method = HTTP_METHOD_POST,
		.use_global_ca_store = certificate_GetUsage(),
		.timeout_ms = 5000
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);

	struct log_entry_info info;

	if(entry_content == NULL){
		entry_content = malloc(DIAGNOSTICS_LOG_ENTRY_CONTENT_OUT_SIZE);

		if(entry_content == NULL){
			result = ESP_ERR_NO_MEM;
			goto error;
		}
	}

	bool reading_wrapped = false;
	while(true){
		offset_from = ftell(diagnostics_file);

		result = read_entry(diagnostics_file, &info, entry_content);
		if(result == ESP_OK){
			if(info.timestamp > upload_info.log_from){
				break;

			}else if(reading_wrapped && ftell(diagnostics_file) >= header.write_offset){
				result = ESP_ERR_NOT_FOUND;
			}

		}else if(result == ESP_ERR_NOT_FOUND && header.wrapped && !reading_wrapped){
			if(fseek(diagnostics_file, header.read_offset, SEEK_SET) != 0){
				printf("Unable to seek to beginning of log when no non-wrapped entry was within requested entry: %s\n", strerror(errno));
				goto error;
			}

			reading_wrapped = true;
		}else{
			printf("Unable to find entry in requested interval: %s\n", esp_err_to_name(result));
			goto error;
		}
	}

	if(fseek(diagnostics_file, offset_from, SEEK_SET) != 0){
		printf("Unable to seek to found entry within range: %s\n", strerror(errno));
		goto error;
	}

	size_t free_block_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	if(free_block_size < CONFIG_ZAPTEC_DIAGNOSTICS_LOG_OCPP_UPLOAD_MALLOC_SIZE + 1024){
		printf("Not enough memory for diagnostics upload\n");
		goto error;
	}

	upload = heap_caps_malloc(CONFIG_ZAPTEC_DIAGNOSTICS_LOG_OCPP_UPLOAD_MALLOC_SIZE, MALLOC_CAP_SPIRAM);
	if(upload == NULL){
		printf("Unable to allocate memory for diagnostics upload\n");
		goto error;
	}

#define BOUNDARY_SIZE 32

	char boundary[BOUNDARY_SIZE];
	snprintf(boundary, BOUNDARY_SIZE, "%*" PRIx32, BOUNDARY_SIZE-1, esp_random());
	//Shouldn't this be a standard way to upload? is it just the server that is incomplete?
	/* esp_http_client_set_header(client, "Content-Type", "text/plain"/\*"application/octet-stream"*\/); */
	/* esp_http_client_set_header(client, "Content-Disposition", "attachment; filename=\"diagnostics_log.txt\""); */

	sprintf(upload, "multipart/form-data; boundary=\"%s\"", boundary);
	esp_http_client_set_header(client, "Content-Type", upload);

	int offset = sprintf(upload, "--%s\r\nContent-Disposition: form-data; filename=\"diagnostics_log.txt\"\r\nContent-Type: text/plain\r\n\r\n", boundary);

	char * upload_pos = upload + offset + 2;

	while(true){
		int remaining_space = (CONFIG_ZAPTEC_DIAGNOSTICS_LOG_OCPP_UPLOAD_MALLOC_SIZE-offset) - (BOUNDARY_SIZE + 8) - 3;
		if(remaining_space <= 0){
			printf("Upload buffer exceeded\n");
			break;
		}

		if(remaining_space >= DIAGNOSTICS_LOG_ENTRY_CONTENT_OUT_SIZE){
			result =  read_entry(diagnostics_file, &info, upload_pos); // TODO: handle "remaining space"
		}else{
			result =  read_entry(diagnostics_file, &info, entry_content);
			if(result == ESP_OK){
				if(info.length >= remaining_space){
					strcpy(upload_pos, entry_content);
				}else{
					result = ESP_ERR_INVALID_SIZE;
				}
			}
		}

		if(result == ESP_OK){
			if(info.timestamp > upload_info.log_to){
				break;
			}else{
				*(upload + offset) = char_from_esp_log_level(info.severity);
				*(upload + offset+1) = ' ';
				offset = offset + info.length + 2;
				upload[offset++] = '\n';
				upload_pos = upload+offset + 2;
			}

			if(reading_wrapped && ftell(diagnostics_file) >= header.write_offset)
				break;

		}else if(result == ESP_ERR_NOT_FOUND){
			if(header.wrapped && !reading_wrapped){
				if(fseek(diagnostics_file, OFFSET_CONTENT, SEEK_SET) != 0){
					printf("Unable to seek to beginning of log to read wrapped entry\n");
					break;
				}

				reading_wrapped = true;
			}else{
				break;
			}


		}else{
			printf("Unable to read next entry: %s\n", esp_err_to_name(result));
			break;
		}
	}

	upload[offset] = '\0';

	sprintf(upload+offset, "\r\n--%s--\r\n", boundary);

	int post_len = strlen(upload);

	esp_err_t err = esp_http_client_open(client, post_len);

	if(err != ESP_OK){
		printf("Unable to open client for upload: %s", esp_err_to_name(err));
		goto error;
	}

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOADING);

	if(esp_http_client_write(client, upload, post_len) != post_len){
		printf("Unable to write bondry for start\n");
		goto error;
	}

	free(upload);
	upload = NULL;

	if(esp_http_client_close(client) != ESP_OK)
		printf("Failed to close client\n");

	if(esp_http_client_cleanup(client) != ESP_OK)
		printf("Failed to cleanup client\n");

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOADED);

	free(upload_info.location);
	upload_info.location = NULL;

	if(xTimerDelete(diagnostics_upload_handle, pdMS_TO_TICKS(1000) == pdPASS))
		diagnostics_upload_handle = NULL;

	xSemaphoreGive(file_lock);

	return;
error:
	free(upload);

	update_diagnostics_status(eOCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED);

	if(upload_info.retries > 1){
		upload_info.retries--;
		if(xTimerChangePeriod(diagnostics_upload_handle, pdMS_TO_TICKS(upload_info.interval * 1000), 0) != pdPASS)
			printf("Unable to change period of diagnostics upload\n");

		if(xTimerReset(diagnostics_upload_handle, 0) != pdPASS){
			printf("Unable to retry diagnostics upload\n");
			free(upload_info.location);
			upload_info.location = NULL;
			diagnostics_upload_handle = NULL;
		}

	}else{
		free(upload_info.location);
		upload_info.location = NULL;

		if(xTimerDelete(diagnostics_upload_handle, pdMS_TO_TICKS(1000) == pdPASS))
			diagnostics_upload_handle = NULL;
	}

	if(release_lock)
		xSemaphoreGive(file_lock);

}

void get_diagnostics_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	printf("Received request for get diagnostics\n");
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
	char * offset = strchr(upload_info.location, ':');
	if(offset == NULL){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		sprintf(err_str, "'location' does not contain a protocol");
		goto error;
	}

	if(!((offset - upload_info.location == 4 && strncmp(upload_info.location, "http", 4) == 0)
			|| (offset - upload_info.location == 5 && strncmp(upload_info.location, "https", 5) == 0))){

		err = eOCPPJ_ERROR_NOT_SUPPORTED;
		sprintf(err_str, "'location' protocol not supported. Currently only 'http' and 'https' are accepted");
		goto error;
	}

	if(strlen(upload_info.location) > 2000){
		err = eOCPPJ_ERROR_NOT_SUPPORTED;
		sprintf(err_str, "Firmware does not support 'location' longer than 2000 characters");
		goto error;
	}

	const unsigned char * uri_end = NULL;
	if(!rfc3986_is_valid_uri((unsigned char *)upload_info.location, &uri_end) || uri_end[0] != '\0'){
		err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		sprintf(err_str, "'location' does not contain a valid URI");
		goto error;
	}

	// If we accept the location url we need to copy it to allow cJSON_Delete to free the request payload
	upload_info.location = strdup(upload_info.location);
	if(upload_info.location == NULL){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "Unable to allocate space for 'location'");
		goto error;
	}

	err = ocppj_get_int_field(payload, "retries", false, &upload_info.retries, err_str, sizeof(err_str));

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

	err = ocppj_get_int_field(payload, "retryInterval", false, &upload_info.interval, err_str, sizeof(err_str));

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

	err = ocppj_get_string_field(payload, "startTime", false, &value_str, err_str, sizeof(err_str));

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

	diagnostics_upload_handle = xTimerCreate("upload", pdMS_TO_TICKS(1000), pdFALSE, NULL, upload_diagnostics_ocpp);
	if(diagnostics_upload_handle == NULL){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "unable to create diagnostics upload timer");
		goto error;
	}

	if(xTimerStart(diagnostics_upload_handle, 0) != pdPASS){
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "unable to start diagnostics upload timer");

		if(xTimerDelete(diagnostics_upload_handle, pdMS_TO_TICKS(1000) == pdPASS))
			diagnostics_upload_handle = NULL;

		goto error;
	}

	cJSON * reply = ocpp_create_get_diagnostics_confirmation(unique_id, "diagnostics_log.txt");
	if(reply == NULL){
		printf("Unable to create get diagnostics confirmation\n");
	}else{
		send_call_reply(reply);
	}

	return;

error:
	if(err == eOCPPJ_NO_ERROR && err == eOCPPJ_NO_VALUE){
		printf("get_diagnostics_cb unexpected termination without known error\n");
	}else{
		printf("get_diagnostics_cb encountered error %d: %s\n", err, err_str);
	}

	cJSON * error_reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(error_reply == NULL){
		printf("Unable to create error reply\n");
	}else{
		send_call_reply(error_reply);
	}
}

static vprintf_like_t default_esp_log = NULL;

int custom_log_function(const char * fmt, va_list ap){
	if(!filesystem_is_ready()){
		return ESP_ERR_INVALID_STATE;
	}

	if(xSemaphoreTake(file_lock, 0) != pdTRUE){
		return ESP_ERR_TIMEOUT;
	}

	/* esp_err_t err =  */diagnostics_log_write(fmt, ap);
	/* if(err != ESP_OK) */
	/* 	printf("log error: %s\n", esp_err_to_name(err)); */

	if(default_esp_log != NULL)
		default_esp_log(fmt, ap);

	xSemaphoreGive(file_lock);
	return 0;
}

esp_err_t diagnostics_log_init(){
	struct stat st;

	if(stat(CONFIG_ZAPTEC_DIAGNOSTICS_LOG_MOUNT_POINT, &st) != 0){
		return ESP_ERR_INVALID_STATE;
	}

	if(file_lock == NULL){
		file_lock = xSemaphoreCreateMutex();
		if(file_lock == NULL){
			return ESP_ERR_NO_MEM;
		}

	}else{
		if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2500)) != pdTRUE){
			return ESP_FAIL;
		}
	}

	if(diagnostics_file !=  NULL){
		if(ferror(diagnostics_file) != 0){
			fclose(diagnostics_file);
			diagnostics_file = NULL;
		}
	}

	esp_err_t ret = ESP_OK;

	if(diagnostics_file == NULL){
		if(stat(file_path, &st) != 0){
			diagnostics_file = fopen(file_path, "wb+");
			if(diagnostics_file == NULL){
				vSemaphoreDelete(file_lock);
				ret = ESP_FAIL;
			}else{
				header.version = 1;
				header.write_offset = OFFSET_CONTENT;
				header.read_offset = OFFSET_CONTENT;
				header.wrapped = false;

				ret = write_header(diagnostics_file, &header);
				if(ret != ESP_OK){
					remove(file_path);
				}else{
					is_header_valid = true;
				}
			}
		}else{
			diagnostics_file = fopen(file_path, "rb+");
			if(diagnostics_file == NULL){
				vSemaphoreDelete(file_lock);
				ret = ESP_FAIL;
			}else{
				if(update_header_if_required() != ESP_OK){

					header.version = 1;
					header.write_offset = OFFSET_CONTENT;
					header.read_offset = OFFSET_CONTENT;
					header.wrapped = false;

					esp_err_t header_result = write_header(diagnostics_file, &header);

					if(header_result != ESP_OK){
						ret = header_result;
						remove(file_path);
					}else{
						is_header_valid = true;
					}
				}
			}
		}

		last_sync = time(NULL);
	}

	if(ret == ESP_OK && default_esp_log == NULL){
		default_esp_log = esp_log_set_vprintf(custom_log_function);
	}

	xSemaphoreGive(file_lock);
	return ret;
}

esp_err_t diagnostics_log_deinit(){

	if(!filesystem_is_ready()){
		return ESP_ERR_INVALID_STATE;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2500)) != pdTRUE)
		return ESP_ERR_TIMEOUT;

	fclose(diagnostics_file);
	diagnostics_file = NULL;

	free(entry_content);
	entry_content = NULL;
	is_header_valid = false;

	if(default_esp_log != NULL)
		esp_log_set_vprintf(default_esp_log);

	default_esp_log = NULL;

	xSemaphoreGive(file_lock); // We do not Delete the semaphore as it would require us to guarantee that no tasks are blocking on it.

	return ESP_OK;
}

#endif /* CONFIG_ZAPTEC_DIAGNOSTICS_LOG */
