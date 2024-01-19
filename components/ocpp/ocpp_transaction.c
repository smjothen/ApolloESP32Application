#include <dirent.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_crc.h"
#include "esp_log.h"

#include "ocpp_transaction.h"
#include "messages/call_messages/ocpp_call_request.h"

#include "types/ocpp_meter_value.h"
#include "types/ocpp_id_token.h"
#include "types/ocpp_charge_point_error_code.h"

static const char * TAG = "OCPP OFFLINETXN";

static SemaphoreHandle_t file_lock = NULL;

#define DIRECTORY_PATH CONFIG_OCPP_FILE_PATH "/txn"

QueueHandle_t ocpp_transaction_call_queue = NULL; // transactions SHOULD be delivered as soon as possible, in chronological order, MUST queue when offline

static bool last_from_file = false;

ocpp_result_callback start_result_cb = NULL;
ocpp_error_callback start_error_cb;

ocpp_result_callback stop_result_cb;
ocpp_error_callback stop_error_cb;

ocpp_result_callback meter_result_cb;
ocpp_error_callback meter_error_cb;

static TaskHandle_t task_to_notify = NULL;
static uint task_notify_offset = 0;

int known_message_count = -1;

static bool filesystem_is_ready(){
	if(file_lock == NULL){
		ESP_LOGD(TAG, "File lock was not initialized");
		return false;
	}

	struct stat st;
	if(stat(DIRECTORY_PATH, &st) != 0){
		ESP_LOGD(TAG, "Directory does not exist");
		return false;
	}

	return true;
}

void ocpp_transaction_set_callbacks(
	ocpp_result_callback start_transaction_result_cb, ocpp_error_callback start_transaction_error_cb,
	ocpp_result_callback stop_transaction_result_cb, ocpp_error_callback stop_transaction_errror_cb,
	ocpp_result_callback meter_transaction_result_cb, ocpp_error_callback meter_transaction_error_cb){

	start_result_cb = start_transaction_result_cb;
	start_error_cb = start_transaction_error_cb;

	stop_result_cb = stop_transaction_result_cb;
	stop_error_cb = stop_transaction_errror_cb;

	meter_result_cb = meter_transaction_result_cb;
	meter_error_cb = meter_transaction_error_cb;

	if(filesystem_is_ready() && task_to_notify != NULL){
		if(ocpp_transaction_get_oldest_timestamp() < LONG_MAX)
			xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);
	}
}

// The related functions do not verify if get/set overwrites front with back or back with front
struct timestamp_queue{
	time_t timestamps[CONFIG_OCPP_MAX_TRANSACTION_QUEUE_SIZE];
	int front;
	int back;
	bool emptying;
};

static struct timestamp_queue txn_enqueue_timestamps = {
	.front = -1,
	.back = -1,
	.emptying = true
};

static void set_txn_enqueue_timestamp(time_t timestamp){
	txn_enqueue_timestamps.emptying = false;
	txn_enqueue_timestamps.back++;
	if(txn_enqueue_timestamps.back >= CONFIG_OCPP_MAX_TRANSACTION_QUEUE_SIZE)
		txn_enqueue_timestamps.back = 0;

	txn_enqueue_timestamps.timestamps[txn_enqueue_timestamps.back] = timestamp;
}

static time_t get_txn_enqueue_timestamp(){
	txn_enqueue_timestamps.emptying = true;
	txn_enqueue_timestamps.front++;
	if(txn_enqueue_timestamps.front >= CONFIG_OCPP_MAX_TRANSACTION_QUEUE_SIZE)
		txn_enqueue_timestamps.front = 0;

	return txn_enqueue_timestamps.timestamps[txn_enqueue_timestamps.front];
}

BaseType_t ocpp_transaction_queue_send(struct ocpp_call_with_cb ** message, TickType_t wait){
	ESP_LOGW(TAG, "Enqueued transaction message. Expecting meter values and will not be stored on file");

	if (!filesystem_is_ready()) {
		ESP_LOGE(TAG, "Filesystem not ready to enqueue message to queue");
		return -1;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Unable to take lock to add to transaction queue");
		return -1;
	}

	BaseType_t result = pdFALSE;
	if(ocpp_transaction_call_queue != NULL)
		result = xQueueSendToBack(ocpp_transaction_call_queue, message, wait);

	if(result == pdTRUE){
		set_txn_enqueue_timestamp(time(NULL));
		known_message_count++;
	}

	xSemaphoreGive(file_lock);
	return result;
}

static time_t peek_txn_enqueue_timestamp(){
	if(txn_enqueue_timestamps.front == txn_enqueue_timestamps.back && txn_enqueue_timestamps.emptying == true) // No data to peek or get/set missused
		return LONG_MAX;

	int position = txn_enqueue_timestamps.front +1;
	if(position == CONFIG_OCPP_MAX_TRANSACTION_QUEUE_SIZE)
		position = 0;

	return txn_enqueue_timestamps.timestamps[position];
}

struct transaction_header{
	bool is_active; // If currently charging or StopRequest has not been written
	time_t start_timestamp; // timestamp of start message. Used to keep transactions chronological
	int transaction_id;
	size_t awaiting_message_count; // Count of messages written that has not gotten a .conf
	long confirmed_offset; // Where to read next awaiting message. End of last message gotten a .conf
};

struct start_transaction_data{
	int connector_id;
	int meter_start;
	int reservation_id;
	bool valid_reservation;
	ocpp_id_token id_tag;
};

struct stop_transaction_data{
	int meter_stop;
	time_t timestamp;
	enum ocpp_reason_id reason_id;
	ocpp_id_token id_tag;
	bool token_is_valid;
};

#define OFFSET_HEADER sizeof(uint8_t)
#define OFFSET_START_TRANSACTION OFFSET_HEADER + sizeof(struct transaction_header) + sizeof(uint32_t)
#define OFFSET_STOP_TRANSACTION OFFSET_START_TRANSACTION + sizeof(struct start_transaction_data) + sizeof(uint32_t)
#define OFFSET_METER_VALUES OFFSET_STOP_TRANSACTION + sizeof(struct stop_transaction_data) + sizeof(uint32_t)

esp_err_t read_header(FILE * fp, struct transaction_header * header_out);

/*
 * Several functions need to itterate over each transaction file.
 * file_loop_function and foreach_transaction_file helps with this. foreach_transact_file will call
 * the given file_loop_function until the file_loop_function returns false or all transaction files
 * have been supplied. If should_open is true, then foreach_transaction_file will open the file before
 * calling the file_loop_function and close the file after. If should_open is false then fp parameter
 * will be NULL. buffer parameter can be used as input/output parameter.
 */
typedef bool (*file_loop_function) (FILE * fp, const char * file_path, int entry, void * buffer);

esp_err_t foreach_transaction_file(file_loop_function file_function, void * buffer, bool should_open){
	ESP_LOGI(TAG, "Attempting to loop through transaction files");

	DIR * dir = opendir(DIRECTORY_PATH);
	if(dir == NULL){
		ESP_LOGE(TAG, "Unable to open directory (%s) to loop", DIRECTORY_PATH);
		return ESP_FAIL;
	}

	struct dirent * dp = NULL;

	char file_path[35];
	FILE * fp = NULL;

	dp = readdir(dir);

	while(dp){
		if(dp->d_type == DT_REG){
			sprintf(file_path, "%s/%.12s", DIRECTORY_PATH, dp->d_name);
			ESP_LOGI(TAG, "Loop found '%s'", file_path);

			int entry = (int)strtol(dp->d_name, NULL, 0);
			if(entry < 0 && entry >= CONFIG_OCPP_MAX_TRANSACTION_FILES){
				ESP_LOGW(TAG, "Found file is not an expected transaction file");
			}else{

				if(should_open){
					fp = fopen(file_path, "rb");
				}

				file_function(fp, file_path, entry, buffer);

				if(should_open && fp != NULL){
					fclose(fp);
					fp = NULL;
				}
			}
		}

		dp = readdir(dir);
	}

	closedir(dir);
	ESP_LOGI(TAG, "End of transaction file loop");
	return ESP_OK;
}

/*
 * The age of a transaction is determined by StrartTransaction.req timestamp or the files creation time, but this is not stored in the filesystem.
 * Only timestamp stored on the file system is st_mtime (last modification). As modifications occure both when adding a message and on response to
 * indicate message has been completed, there is no way to tell from modification time alone if modified due to .conf message or .req created and multiple
 * new transaction could be created before all messages of one transaction has been sent.
 * The start time of the transaction is therefore stored in the file and all files need to be read to find the oldest file.
 *
 * To prevent having to read all remaining files after all messages of a transaction has gotten a .conf, we cache some of the oldest timestamps
 */
struct entry_timestamp{
	int entry;
	time_t timestamp;
};

#define OLDEST_CACH_SIZE 5
struct entry_timestamp oldest_known_entries[OLDEST_CACH_SIZE] = {{.entry = -1}};

bool populate_oldest(FILE * fp, const char * file_path, int entry, void * buffer){
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open '%s' to populate oldest transaction files", file_path);
		return true;
	}

	struct transaction_header header;
	if(read_header(fp, &header) != ESP_OK){
		ESP_LOGE(TAG, "Unable to read header to populate oldest transaction files");
		return true;
	}

	for(size_t i = 0; i < OLDEST_CACH_SIZE; i++){
		if(oldest_known_entries[i].entry == -1 || oldest_known_entries[i].timestamp > header.start_timestamp){
			struct entry_timestamp tmp;

			for(;i < OLDEST_CACH_SIZE; i++){
				tmp.entry =  oldest_known_entries[i].entry;
				tmp.timestamp = oldest_known_entries[i].timestamp;

				oldest_known_entries[i].entry = entry;
				oldest_known_entries[i].timestamp = header.start_timestamp;

				entry = tmp.entry;
				header.start_timestamp = tmp.timestamp;
			}
		}
	}

	return true;
}

esp_err_t find_oldest_transaction_file(int * entry_out, time_t * timestamp_out)
{
	ESP_LOGI(TAG, "Attempting to find oldest transaction");

	char file_path[32];
	struct stat st;

	while(oldest_known_entries[0].entry != -1){
		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, oldest_known_entries[0].entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);
		if(stat(file_path, &st) == 0){
			*timestamp_out = oldest_known_entries[0].timestamp;
			*entry_out = oldest_known_entries[0].entry;

			return ESP_OK;
		}

		for(size_t i = 0; i < OLDEST_CACH_SIZE; i++){
			if(i == OLDEST_CACH_SIZE -1){
				oldest_known_entries[i].entry = -1;
				oldest_known_entries[i].timestamp = 0;
			}else{
				oldest_known_entries[i].entry = oldest_known_entries[i+1].entry;
				oldest_known_entries[i].timestamp = oldest_known_entries[i+1].timestamp;
			}

			if(oldest_known_entries[i].entry == -1)
				break;
		}
	}

	esp_err_t ret = foreach_transaction_file(populate_oldest, NULL, true);
	if(ret != ESP_OK){
		ESP_LOGE(TAG, "Unable to loop over transaction files to find oldest");
		for(size_t i = 0; i < OLDEST_CACH_SIZE; i++){
			oldest_known_entries[i].entry = -1;
			oldest_known_entries[i].timestamp = LONG_MAX;
		}
	}else if(oldest_known_entries[0].entry == -1){
		ret = ESP_ERR_NOT_FOUND;
	}else{
		ESP_LOGI(TAG, "Oldest transaction is at entry: %d", oldest_known_entries[0].entry);
	}

	*timestamp_out = oldest_known_entries[0].timestamp;
	*entry_out = oldest_known_entries[0].entry;

	return ret;
}

int find_next_vacant_entry(){

	char file_path[32];
	struct stat st;

	for(int i = 0; i < CONFIG_OCPP_MAX_TRANSACTION_FILES; i++){
		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, i % CONFIG_OCPP_MAX_TRANSACTION_FILES);

		ESP_LOGI(TAG, "Checking for transaction at: %s", file_path);

		if(stat(file_path, &st) != 0)
			return i;
	}

	ESP_LOGE(TAG, "Unable to find vacant entry");
	return -1;
}

bool count_files(FILE * fp, const char * file_path, int entry, void * buffer){
	int * count = (int *)buffer;

	*count = *count +1;
	return true;
}

int ocpp_transaction_count()
{
	ESP_LOGI(TAG, "Counting transaction files");
	if (!filesystem_is_ready()) {
		ESP_LOGE(TAG, "Filesystem not ready to count transactions");
		return -1;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Unable to take lock to count transaction files");
		return -1;
	}

	int count = 0;
	if(foreach_transaction_file(count_files, &count, false) != ESP_OK){
		ESP_LOGE(TAG, "Unable to loop over transaction file to get count");
		count = -1;
	}
	xSemaphoreGive(file_lock);

	return count;
}

esp_err_t read_header(FILE * fp, struct transaction_header * header_out){

	if(fseek(fp, OFFSET_HEADER, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to header during read: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(fread(header_out, sizeof(struct transaction_header), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read header: %s", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read header crc: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(crc != esp_crc32_le(0, (uint8_t *)header_out, sizeof(struct transaction_header))){
		ESP_LOGE(TAG, "crc mismatch for header: %s", strerror(errno));
		return ESP_ERR_INVALID_CRC;
	}

	return ESP_OK;
}

bool count_messages(FILE * fp, const char * file_path, int entry, void * buffer){
	int * count = (int *)buffer;

	if(fp != NULL){
		struct transaction_header header;

		if(read_header(fp, &header) == ESP_OK){
			ESP_LOGI(TAG, "file message count: %zu", header.awaiting_message_count);
			*count += header.awaiting_message_count;
		}else{
			ESP_LOGE(TAG, "Unable to read header to get transaction message count");
		}
	}else{
		ESP_LOGE(TAG, "Unable to open '%s' to read message count", file_path);
	}

	return true;
}

size_t ocpp_transaction_message_count(){

	if(known_message_count >= 0)
		return known_message_count;

	ESP_LOGI(TAG, "Counting transaction messages");

	if (!filesystem_is_ready()) {
		ESP_LOGE(TAG, "Filesystem not ready to count messages");
		return 0;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Unable to take lock to count messages");
		return -1;
	}

	known_message_count = 0;
	if(foreach_transaction_file(count_messages, &known_message_count, true) != ESP_OK){
		ESP_LOGE(TAG, "Unable to loop over transaction files to get message count");
		known_message_count = -1;
	}

	if(ocpp_transaction_call_queue != NULL){
		known_message_count += uxQueueMessagesWaiting(ocpp_transaction_call_queue);
	}

	xSemaphoreGive(file_lock);

	ESP_LOGI(TAG, "Transaction message count: %d", known_message_count);

	return known_message_count;
}

esp_err_t read_start_transaction(FILE * fp, int * connector_id_out, ocpp_id_token id_tag_out,
				int * meter_start_out, int * reservation_id_out, bool * valid_reservation_out, time_t * timestamp_out){
	struct transaction_header header;

	if(read_header(fp, &header) != ESP_OK){
		ESP_LOGE(TAG, "Unable to read header during start transaction read");
		return ESP_FAIL;
	}

	if(fseek(fp, OFFSET_START_TRANSACTION, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to start transaction during read");
		return ESP_FAIL;
	}

	struct start_transaction_data data;
	if(fread(&data, sizeof(struct start_transaction_data), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read start transaction: %s", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read start transaction crc");
		return ESP_FAIL;
	}

	if(crc != esp_crc32_le(0, (uint8_t *)&data, sizeof(struct start_transaction_data))){
		ESP_LOGE(TAG, "crc mismatch for start transaction");
		return ESP_ERR_INVALID_CRC;
	}

	*connector_id_out = data.connector_id;
	data.id_tag[20] = '\0';
	strcpy(id_tag_out, data.id_tag);
	*meter_start_out = data.meter_start;
	*valid_reservation_out = data.valid_reservation;
	if(*valid_reservation_out){
		*reservation_id_out = data.reservation_id;
	}
	*timestamp_out = header.start_timestamp;

	return ESP_OK;
}

#define MAX_METER_VALUE_LENGTH 16384

struct meter_crc_buffer{
	uint32_t crc_meter_data;
	time_t timestamp;
};

esp_err_t read_meter_value_string(FILE * fp, unsigned char ** meter_data, size_t * meter_data_length, time_t * timestamp){

	if(fread(meter_data_length, sizeof(size_t), 1, fp) != 1){
		if(ferror(fp) == 0){
			ESP_LOGI(TAG, "No meter value");
			return ESP_ERR_NOT_FOUND;
		}

		ESP_LOGE(TAG, "Unable to read meter value length: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(*meter_data_length > MAX_METER_VALUE_LENGTH){
		ESP_LOGE(TAG, "Meter value on file exceed max length: %zu > %d", *meter_data_length, MAX_METER_VALUE_LENGTH);
		return ESP_FAIL;
	}

	*meter_data = malloc(*meter_data_length);
	if(*meter_data == NULL){
		ESP_LOGE(TAG, "Unable to allocate buffer for meter value");
		return ESP_ERR_NO_MEM;
	}

	if(fread(*meter_data, *meter_data_length, 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read meter value: %s", strerror(errno));
		goto error;
	}

	if(fread(timestamp, sizeof(time_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read meter value timestamp");
		goto error;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read meter value crc");
		goto error;
	}

	struct meter_crc_buffer crc_buffer = {0};

	crc_buffer.crc_meter_data = esp_crc32_le(0, (uint8_t *)*meter_data, *meter_data_length);
	crc_buffer.timestamp = *timestamp;

	if(crc != esp_crc32_le(0, (uint8_t *)&crc_buffer, sizeof(struct meter_crc_buffer))){
		ESP_LOGE(TAG, "crc mismatch for meter value");
		goto error;
	}

	return ESP_OK;
error:
	free(*meter_data);
	*meter_data = NULL;

	return ESP_FAIL;
}

esp_err_t read_stop_transaction(FILE * fp, struct stop_transaction_data * stop_transaction_out){
	if(fseek(fp, OFFSET_STOP_TRANSACTION, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to stop transaction during read");
		return ESP_FAIL;
	}

	if(fread(stop_transaction_out, sizeof(struct stop_transaction_data), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read stop transaction: %s", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read crc for stop transaction: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(crc != esp_crc32_le(0, (uint8_t *)stop_transaction_out, sizeof(struct stop_transaction_data))){
		ESP_LOGE(TAG, "crc mismatch for stop transaction");
		return ESP_ERR_INVALID_CRC;
	}

	stop_transaction_out->id_tag[20] = '\0';
	return ESP_OK;
}

esp_err_t write_header(FILE * fp, bool * is_active, time_t * start_transaction, int * transaction_id, long * confirmed_offset,
		int message_count, bool set_count){

	struct transaction_header header = {0};
	if(is_active == NULL || start_transaction == NULL || transaction_id == NULL || confirmed_offset == NULL || !set_count){

		if(read_header(fp, &header) != ESP_OK){
			ESP_LOGE(TAG, "Unable to read header during write update");
			return ESP_FAIL;
		}

		if(is_active != NULL)
			header.is_active = *is_active;

		if(start_transaction != NULL)
			header.start_timestamp = *start_transaction;

		if(transaction_id != NULL)
			header.transaction_id = *transaction_id;

		if(confirmed_offset != NULL)
			header.confirmed_offset = *confirmed_offset;

		if(set_count){
			header.awaiting_message_count = message_count;
			known_message_count = -1;

		}else{
			if(message_count < 0 && abs(message_count) > header.awaiting_message_count){ // If would underflow. Set to zero
				header.awaiting_message_count = 0;
				known_message_count = -1;
			}else{
				if(known_message_count >= 0)
					known_message_count += message_count;

				header.awaiting_message_count += message_count;
			}
		}
	}else{
		header.is_active = *is_active;
		header.start_timestamp = *start_transaction;
		header.transaction_id = *transaction_id;
		header.confirmed_offset = *confirmed_offset;
		header.awaiting_message_count = message_count;
		known_message_count = -1;
	}

	if(header.confirmed_offset < 0 || header.confirmed_offset > CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE){
		ESP_LOGE(TAG, "Unable to write confirmed offset out of range: %ld. Will write max allowed", header.confirmed_offset);
		header.confirmed_offset = CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE;
	}

	if(fseek(fp, OFFSET_HEADER, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to header during write");
		return ESP_FAIL;
	}

	if(fwrite(&header, sizeof(struct transaction_header), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write header for %s transaction: %s", header.is_active ? "active" : "inactive", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&header, sizeof(struct transaction_header));

	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write header crc");
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t write_start_transaction(FILE * fp, int connector_id, const ocpp_id_token id_tag,
				int meter_start, int reservation_id, bool valid_reservation, time_t timestamp){

	bool is_active = true;
	int transaction_id = -1;
	long confirmed_offset = OFFSET_START_TRANSACTION;

	write_header(fp, &is_active, &timestamp, &transaction_id, &confirmed_offset, 1, true);

	struct start_transaction_data  data = {
		.connector_id = connector_id,
		.meter_start = meter_start,
		.reservation_id = reservation_id,
		.valid_reservation = valid_reservation,
	};
	strncpy(data.id_tag, id_tag, sizeof(ocpp_id_token));

	if(fseek(fp, OFFSET_START_TRANSACTION, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to start transaction during write: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(&data, sizeof(struct start_transaction_data), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write start transaction");
		return ESP_FAIL;
	}

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&data, sizeof(struct start_transaction_data));

	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write start transaction crc");
		return ESP_FAIL;
	}

	return ESP_OK;
}

/*
 * The ocpp specification states that transaction messsages are sent in chronological order, But since we do not have infinite storage or memory,
 * we can not gaurantee that all messages are sent if data exceed capacity. Current approach attempts to limit writes to file for meter values because
 * writes may be too slow if offset for next read needs to be updated after every MeterValues.conf while delay is high and SampleInterval/ClockAlignedDataInterval
 * is frequent.
 *
 * Unlike StartTransaction and StopTransaction, MeterValues do not change the charging state and should therefore not cause significant problems if missing.
 * One problem could be if ESP reboots without stopping charging and interval messurand does not have a start point for next sample. Interval meassurands should
 * have a timestamp for the start of measurement, not for the end when it is sent. Steve CS used for testing did not accept duration timestamps.
 *
 * Since the meter values may be in queue (memory) or on file (storage), each meter value needs a enqueued timestamp to ensure chronological order.
 *
 * Since the lenght of meter_data can be quite large, would not want to move/copy it to create the crc. The crc is therefore first calculated from the
 * meter_data, and then that crc is copied together with length and timestamp to a new buffer used to create the written crc.
 */
//Consider using to overwrite completed messages and using truncate to ensure last

esp_err_t write_meter_value_string(FILE * fp, const unsigned char * meter_data, size_t meter_data_length, time_t timestamp, bool stop_related){

	if(meter_data_length > MAX_METER_VALUE_LENGTH){
		ESP_LOGE(TAG, "Rejecting write of meter data with excessive length: %zu > %d", meter_data_length, MAX_METER_VALUE_LENGTH);
		return ESP_ERR_INVALID_SIZE;
	}

	if(meter_data_length <= 0){
		ESP_LOGE(TAG, "Rejecting write of meter data with no or invalid length: %zu <= 0", meter_data_length);
		return ESP_ERR_INVALID_SIZE;
	}

	if(fseek(fp, 0, SEEK_END) != 0){
		ESP_LOGE(TAG, "Unable to seek to end to get new meter value position: %s", strerror(errno));
		return ESP_FAIL;
	}

	long offset = ftell(fp);
	if(offset == -1){
		ESP_LOGE(TAG, "Unable to get EOF position for writing meter value: %s", strerror(errno));
		return ESP_FAIL;
	}

	offset = (offset > OFFSET_METER_VALUES) ? offset : OFFSET_METER_VALUES;

	if(sizeof(meter_data_length) + meter_data_length + offset + sizeof(uint32_t) > CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE){
		ESP_LOGE(TAG, "Rejecting write of meter data due to file size limit: %ld > %d", meter_data_length + offset , CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE);
		return ESP_ERR_INVALID_SIZE;
	}

	if(write_header(fp, NULL, NULL, NULL, NULL, stop_related ? 0 : 1, false) != ESP_OK){
		ESP_LOGE(TAG, "Unable to update header during stop transaction write");
		return ESP_FAIL;
	}

	if(fseek(fp, offset, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to add meter value: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(&meter_data_length, sizeof(size_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write meter value length: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(meter_data, meter_data_length, 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write meter value: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(fwrite(&timestamp, sizeof(time_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to meter value timestamp: %s", strerror(errno));
		return ESP_FAIL;
	}

	struct meter_crc_buffer crc_buffer = {0};

	crc_buffer.crc_meter_data = esp_crc32_le(0, (uint8_t *)meter_data, meter_data_length);
	crc_buffer.timestamp = timestamp;

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&crc_buffer, sizeof(struct meter_crc_buffer));

	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write meter value crc");
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t write_stop_transaction(FILE * fp, const char * id_tag, int meter_stop, time_t timestamp, enum ocpp_reason_id reason_id){

	bool is_active = false;

	if(write_header(fp, &is_active, NULL, NULL, NULL, 1, false) != ESP_OK){
		ESP_LOGE(TAG, "Unable to update header during stop transaction write");
		return ESP_FAIL;
	}

	struct stop_transaction_data data = {
		.meter_stop = meter_stop,
		.timestamp = timestamp,
		.reason_id = reason_id,
	};

	if(id_tag != NULL){
		data.token_is_valid = true;
		strncpy(data.id_tag, id_tag, sizeof(ocpp_id_token));
	}else{
		data.token_is_valid = false;
	}

	if(fseek(fp, OFFSET_STOP_TRANSACTION, SEEK_SET) != 0){
		ESP_LOGE(TAG, "Unable to seek to write stop transaction");
		return ESP_FAIL;
	}

	if(fwrite(&data, sizeof(struct stop_transaction_data), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write stop transaction");
		return ESP_FAIL;
	}

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&data, sizeof(struct stop_transaction_data));

	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write stop transaction crc");
		return ESP_FAIL;
	}

	return ESP_OK;
}

bool is_active(FILE * fp, const char * file_path, int entry, void * buffer){

	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open file to check if active");
		return true;
	}

	struct transaction_header header;
	if(read_header(fp, &header) != ESP_OK){
		ESP_LOGE(TAG, "Unable to read header to check if active: %s", strerror(errno));
		return true;

	}

	if(header.is_active){
		ESP_LOGI(TAG, "Found active transaction file: '%s'", file_path);
		*((int *)buffer) = entry;
		return false;
	}

	return true;
}

int active_entry = -1;
// TODO: consider checking connector id for consistensy. The connector id should always be one as there only is one connector on the GO.

int find_active_entry(int connector_id){
	ESP_LOGI(TAG, "Call to find active entry");

	if(active_entry != -1){

		FILE * fp;
		char file_path[32];

		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, active_entry);

		ESP_LOGI(TAG, "Checking previous active entry: '%s'", file_path);

		fp = fopen(file_path, "rb");
		if(fp == NULL){
			ESP_LOGW(TAG, "Active entry '%s' could not be opened: %s", file_path, strerror(errno)); // Could have been deleted since last check. Not an error.
		}else{
			struct transaction_header header;
			esp_err_t err = read_header(fp, &header);
			fclose(fp);

			if(err != ESP_OK){
				ESP_LOGE(TAG, "Unable to read header of active entry: %s", strerror(errno));

			}else if(header.is_active){
				return active_entry;
			}
		}

		active_entry = -1; // Last active entry is no longer active
	}

	ESP_LOGI(TAG, "Checking for new active entry. Default result is: %d", active_entry);

	if(foreach_transaction_file(is_active, &active_entry, true) != ESP_OK){
		ESP_LOGE(TAG, "Unable to loop over transaction files to get active entry");
	}

	ESP_LOGI(TAG, "Active entry result: %d", active_entry);
	return active_entry;
}

int ocpp_transaction_find_active_entry(int connector_id){

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "File system not ready to get active entry");
		return -1;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Unable to take lock to find active entry");
		return -1;
	}

	int result = find_active_entry(connector_id);

	xSemaphoreGive(file_lock);

	return result;
}

enum transaction_message_type_id{
	eTRANSACTION_TYPE_START,
	eTRANSACTION_TYPE_STOP,
	eTRANSACTION_TYPE_METER
};

struct transaction_header loaded_transaction_header;
time_t loaded_transaction_timestamp = LONG_MAX;
void * loaded_transaction_data = NULL;
unsigned char * loaded_transaction_stop_meter_data = NULL;
size_t loaded_transaction_size;
int loaded_transaction_entry = -1;
long loaded_transaction_on_confirmed_offset;
enum transaction_message_type_id loaded_transaction_type;

void loaded_transaction_reset(){
	loaded_transaction_timestamp = LONG_MAX;
	free(loaded_transaction_data);
	loaded_transaction_data = NULL;
	loaded_transaction_entry = -1;
	free(loaded_transaction_stop_meter_data);
	loaded_transaction_stop_meter_data = NULL;
}

/*
 * This function will try to correct the file as much as possible to keep the transaction state consistent if possible.
 * If this funcion fails or is incorrect then it is possible that the charger would have a persistent fault that could
 * conceivably lead to an infinite boot loop. e.g if the charger boots and attaches to ocpp, then checks for a stored
 * transaction. Reads a faulty transaction and crashes as a resul. Then it would reboot and try the same again.
 *
 * The function should therefore always make some change, so that it is less likely for the issue to be persistent.
 */
void fail_transaction_message_on_file(const char * file_path){

	ESP_LOGE(TAG, "Failing transaction on file '%s' . Looking for error to minimise effect on transaction state.", file_path);
	known_message_count = -1;

	struct transaction_header header;
	header.transaction_id = -1;

	ESP_LOGW(TAG, "Checking if fault is with opening file:");
	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Failed: %s", strerror(errno));
		goto error;
	}else{
		ESP_LOGI(TAG, "Succeeded");
	}

	ESP_LOGW(TAG, "Checking if fault is with reading header:");
	esp_err_t ret = read_header(fp, &header);

	if(ret != ESP_OK){
		ESP_LOGE(TAG, "Failed");
		goto error;
	}else{
		ESP_LOGI(TAG, "Succeeded");
	}

	struct transaction_header new_header = {0};
	new_header.start_timestamp = header.start_timestamp;
	new_header.awaiting_message_count = 0;

	if(header.confirmed_offset <= OFFSET_START_TRANSACTION){
		new_header.transaction_id = -1;
		ESP_LOGI(TAG, "Transaction to fail has not sent start transaction");

		if(header.awaiting_message_count == 0){
			ESP_LOGE(TAG, "Invalid awaiting message count as Start is not sent and not awaiting");
		}

		new_header.awaiting_message_count++;

		ESP_LOGW(TAG, "Checking if fault is with start message");

		struct start_transaction_data * start_data = (struct start_transaction_data *)loaded_transaction_data;
		if(read_start_transaction(fp, &start_data->connector_id, start_data->id_tag, &start_data->meter_start,
						&start_data->reservation_id, &start_data->valid_reservation,
						&loaded_transaction_header.start_timestamp) != ESP_OK){

			ESP_LOGE(TAG, "Failed");
			goto error;
		}else{
			ESP_LOGE(TAG, "Success");
		}
	}else{
		ESP_LOGI(TAG, "Header indicate start complete. Setting transaction id to '%d'", header.transaction_id);
		new_header.transaction_id = header.transaction_id;
	}

	if(header.confirmed_offset > OFFSET_START_TRANSACTION && header.confirmed_offset < OFFSET_METER_VALUES){
		ESP_LOGE(TAG, "Unexpected offset not at a meter value, end or start message");
		new_header.confirmed_offset = OFFSET_METER_VALUES;
	}

	if(!header.is_active){
		new_header.awaiting_message_count++;

		struct stop_transaction_data stop_data;
		ESP_LOGW(TAG, "Checking if fault is with stop message:");
		ret = read_stop_transaction(fp, &stop_data);

		if(ret != ESP_OK){
			ESP_LOGE(TAG, "Failed");
			// We could possibly correct parts of the stop message. stop reason would be lost, but timestamp may be part of last meter value
			// and meterStop could be part of last meter value, start of next transaction on file or current value of no other transaction
			// exists.
			goto error;
		}else{
			ESP_LOGI(TAG, "Success");
		}
	}// else: Could try to overwrite non-existent stop with '\0', but would require logic in read of stop to indicate not found

	new_header.is_active = header.is_active;

	ESP_LOGW(TAG, "Checking issues related to EOF:");
	if(fseek(fp, 0, SEEK_END) != 0){
		ESP_LOGE(TAG, "Unable to seek to end of file: %s", strerror(errno));
		goto error;
	}

	long offset_end = ftell(fp);
	if(offset_end == -1){
		ESP_LOGE(TAG, "Unable to tell file end position");
		goto error;
	}

	if(offset_end > CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE){
		ESP_LOGE(TAG, "EOF is set past max file size");

		offset_end = CONFIG_OCPP_MAX_TRANSACTION_FILE_SIZE;
	}

	if(offset_end < header.confirmed_offset){
		ESP_LOGE(TAG, "Invalid current offset");

		if(header.awaiting_message_count == new_header.awaiting_message_count){
			ESP_LOGE(TAG, "No change would be made by truncating file");
			goto error;
		}

		new_header.confirmed_offset = OFFSET_METER_VALUES;
		if(write_header(fp, &new_header.is_active, &new_header.start_timestamp, &new_header.transaction_id,
					&new_header.confirmed_offset, new_header.awaiting_message_count, true) != ESP_OK){
			ESP_LOGE(TAG, "Unable to write new header");
			goto error;
		}

		fclose(fp);
		fp = NULL;
		if(truncate(file_path, new_header.confirmed_offset) != 0){
			ESP_LOGE(TAG, "Unable to truncate file: %s", strerror(errno));
			goto error;
		}

		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Error with transaction data. MeterValue maybe lost",
					NULL, NULL, true, false);
		return;
	}else if(header.confirmed_offset != OFFSET_STOP_TRANSACTION){

		long meter_position;
		if(header.confirmed_offset <= OFFSET_METER_VALUES){
			meter_position = OFFSET_METER_VALUES;

		}else{
			meter_position = header.confirmed_offset;
		}

		if(fseek(fp, meter_position, SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to meter position");
			goto error;
		}else{
			ESP_LOGI(TAG, "Seek to meter value at %ld success. First meter value should have been at %d", meter_position, OFFSET_METER_VALUES);
		}

		unsigned char * meter_data = NULL;
		size_t meter_length;
		time_t meter_time;

		while(meter_position < offset_end){

			ESP_LOGW(TAG, "Checking meter value at %ld", meter_position);
			esp_err_t ret = read_meter_value_string(fp, &meter_data, &meter_length, &meter_time);
			if(ret != ESP_OK){
				ESP_LOGE(TAG, "Failed (or end)");
				break; // Wheter we get ESP_FAIL or ESP_NOT_FOUND it does not matter. The fix should be the same if possible; update awaiting in header and truncate
			}else{
				ESP_LOGI(TAG, "Success");

				free(meter_data);
				meter_data = NULL;

				long after_read_position = ftell(fp);
				if(after_read_position == -1 || after_read_position <= meter_position){
					ESP_LOGE(TAG, "Unable to confirm read progress");
					goto error;

				}else if(after_read_position > offset_end){
					ESP_LOGE(TAG, "Read past expected EOF");
					break;
				}

				meter_position = after_read_position;
				new_header.awaiting_message_count++;
			}
		}

		new_header.confirmed_offset = meter_position;

		if(new_header.awaiting_message_count == header.awaiting_message_count && new_header.confirmed_offset == header.confirmed_offset
			&& meter_position >= offset_end){
			ESP_LOGE(TAG, "No correctable error detected");
			goto error;
		}

		if(write_header(fp, &new_header.is_active, &new_header.start_timestamp, &new_header.transaction_id,
					&new_header.confirmed_offset, new_header.awaiting_message_count, true) != ESP_OK){
			ESP_LOGE(TAG, "Unable to write new header after checking meter values");
			goto error;
		}

		fclose(fp);
		fp = NULL;
		if(truncate(file_path, new_header.confirmed_offset) != 0){
			ESP_LOGE(TAG, "Unable to truncate file: %s", strerror(errno));
			goto error;
		}

		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Error with transaction data. MeterValue maybe lost",
					NULL, NULL, true, false);
		return;
	}

error:
	ESP_LOGE(TAG, "Unable to fix file. Attempting to delete");
	if(fp != NULL)
		fclose(fp);

	if(remove(file_path) != 0){
		ESP_LOGE(TAG, "Unable to delete transaction file. May be persistently ruined");
		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Error with transaction data. Unable to discard",
					NULL, NULL, true, false);
	}else{
		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Error lead to transaction data being discarded",
					NULL, NULL, true, false);
	}
}

esp_err_t load_next_transaction_message(){

	ESP_LOGI(TAG, "Attempting to load next message");

	if(loaded_transaction_data != NULL){
		ESP_LOGI(TAG, "Already loaded");
		return ESP_OK;
	}

	loaded_transaction_reset();

	char file_path[32];
	FILE * fp = NULL;

	if(loaded_transaction_entry != -1){
		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, loaded_transaction_entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);
	}

	struct stat st;
	if(loaded_transaction_entry == -1 || stat(file_path, &st) != 0){ // If no loaded transaction or loaded file has been deleted
		esp_err_t err = find_oldest_transaction_file(&loaded_transaction_entry, &loaded_transaction_header.start_timestamp);
		if(err != ESP_OK){
			if(err == ESP_ERR_NOT_FOUND){
				ESP_LOGI(TAG, "No transaction file for loading message");
			}else{
				ESP_LOGE(TAG, "Unable to find oldest transaction for loading transaction message");
			}
			return err;
		}

		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, loaded_transaction_entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);
	}

	ESP_LOGI(TAG, "Loading from: '%s'", file_path);

	esp_err_t ret = ESP_FAIL;

	fp = fopen(file_path, "rb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open file to load message: %s", strerror(errno));
		goto cleanup;
	}

	if(read_header(fp, &loaded_transaction_header) != ESP_OK){
		ESP_LOGE(TAG, "Unable to read header to load message.");
		goto cleanup;
	}

	if(loaded_transaction_header.awaiting_message_count == 0){
		ESP_LOGI(TAG, "Loaded transaction has no awaiting messages");
		ret = ESP_ERR_NOT_FOUND;
		goto cleanup;
	}

	if(loaded_transaction_header.confirmed_offset <= OFFSET_START_TRANSACTION){
		if(fseek(fp, OFFSET_START_TRANSACTION, SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to start transaction to load message");
			goto cleanup;
		}

		loaded_transaction_data = malloc(sizeof(struct start_transaction_data));
		if(loaded_transaction_data == NULL){
			ESP_LOGE(TAG, "Unable to create buffer to load start transaction data");
			ret = ESP_ERR_NO_MEM;
			goto cleanup;
		}

		struct start_transaction_data * data = (struct start_transaction_data *)loaded_transaction_data;
		if(read_start_transaction(fp, &data->connector_id, data->id_tag, &data->meter_start,
						&data->reservation_id, &data->valid_reservation,
						&loaded_transaction_header.start_timestamp) != ESP_OK){

			ESP_LOGE(TAG, "Unable to read start transaction into loaded");
			goto cleanup;
		}

		loaded_transaction_type = eTRANSACTION_TYPE_START;
		loaded_transaction_timestamp = loaded_transaction_header.start_timestamp;

		ret = ESP_OK;
	}else{
		if(fseek(fp, loaded_transaction_header.confirmed_offset, SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to next meter value to check for pending message");
			goto cleanup;
		}

		time_t timestamp;

		if(loaded_transaction_header.confirmed_offset == OFFSET_STOP_TRANSACTION){
			ret = ESP_ERR_NOT_FOUND; // Indicate that stop transaction did not have meter value (transactionData)
		}else{
			// May also indicate that no meter value exists for stop transaction
			ret = read_meter_value_string(fp, (unsigned char **)&loaded_transaction_data, &loaded_transaction_size, &timestamp);
		}

		if(ret != ESP_OK){
			if(ret == ESP_ERR_NOT_FOUND){
				if(!loaded_transaction_header.is_active){
					if(fseek(fp, OFFSET_STOP_TRANSACTION, SEEK_SET) != 0){
						ESP_LOGE(TAG, "Unable to seek to stop transaction to load message");
						goto cleanup;
					}

					loaded_transaction_data = malloc(sizeof(struct stop_transaction_data));
					if(loaded_transaction_data == NULL){
						ESP_LOGE(TAG, "Unable to allocate memory for stop transaction data to load message");
						ret = ESP_ERR_NO_MEM;
						goto cleanup;
					}

					struct stop_transaction_data * data = (struct stop_transaction_data *)loaded_transaction_data;
					if(read_stop_transaction(fp, data) != ESP_OK){
						ESP_LOGE(TAG, "Unable to read stop transaction to load message");
						ret = ESP_FAIL;
						goto cleanup;
					}

					loaded_transaction_type = eTRANSACTION_TYPE_STOP;
					loaded_transaction_timestamp = data->timestamp;

					ret = ESP_OK;
				}else{
					ESP_LOGI(TAG, "Requested to load next message, but no new message yet on active transaction");
				}
			}else{
				ESP_LOGE(TAG, "Unable to read meter value to find oldest message");
				goto cleanup;
			}
		}else{
			loaded_transaction_type = eTRANSACTION_TYPE_METER;
			loaded_transaction_timestamp = timestamp;
			loaded_transaction_on_confirmed_offset = ftell(fp);

			bool is_stop_related;
			if(ocpp_is_stop_txn_data_from_contiguous_buffer((unsigned char *)loaded_transaction_data, loaded_transaction_size, &is_stop_related) == ESP_OK && is_stop_related){
				loaded_transaction_stop_meter_data = loaded_transaction_data;
				loaded_transaction_data = malloc(sizeof(struct stop_transaction_data));
				if(loaded_transaction_data == NULL){
					ESP_LOGE(TAG, "Unable to allocate memory for stop transaction when stop txn meter data existed");
					ret = ESP_ERR_NO_MEM;
					goto cleanup;
				}

				struct stop_transaction_data * data = (struct stop_transaction_data *)loaded_transaction_data;
				if(read_stop_transaction(fp, data) != ESP_OK){
					ESP_LOGE(TAG, "Unable to read stop transaction after meter value indicate stop related");
					ret = ESP_FAIL;
					goto cleanup;
				}
				loaded_transaction_type = eTRANSACTION_TYPE_STOP;
			}

			ret = ESP_OK;
		}
	}

cleanup:
	if(fp != NULL)
		fclose(fp);

	if(ret == ESP_FAIL){
		fail_transaction_message_on_file(file_path);
		loaded_transaction_reset();
	}

	return ret;
}

void fail_loaded_transaction(){
	ESP_LOGW(TAG, "Failing loaded transaction");

	free(loaded_transaction_data);
	known_message_count = -1;

	loaded_transaction_data = NULL;

	int entry = loaded_transaction_entry;
	loaded_transaction_entry = -1;

	if(entry == -1){
		ESP_LOGE(TAG, "Fail loaded transaction called with no loaded transaction entry");
		return;
	}

	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);
	ESP_LOGW(TAG, "Loaded transaction to fail: %s", file_path);

	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open loaded transaction entry file");
		goto error;
	}

	struct transaction_header header;
	if(read_header(fp, &header) != ESP_OK){
		ESP_LOGE(TAG, "Unable to read header to fail loaded transaction.");
		goto error;
	}

	if(header.confirmed_offset <= OFFSET_START_TRANSACTION){
		ESP_LOGW(TAG, "Failing from start");
		header.transaction_id = -1;
		header.confirmed_offset = OFFSET_METER_VALUES;

		if(header.awaiting_message_count > 0)
			header.awaiting_message_count--;

		if(write_header(fp, &header.is_active, &header.start_timestamp, &header.transaction_id, &header.confirmed_offset,
					header.awaiting_message_count, true) != ESP_OK){
			ESP_LOGE(TAG, "Unable to fail start message of loaded transaction.");
			goto error;
		}

	}else if(header.confirmed_offset >= OFFSET_METER_VALUES){
		ESP_LOGW(TAG, "Failing from meter value");

		if(header.is_active){
			// Ignore stored meter values after failed value, but allow new meter values to be created.
			header.confirmed_offset = OFFSET_METER_VALUES;
			header.awaiting_message_count = 0;

			if(write_header(fp, &header.is_active, &header.start_timestamp, &header.transaction_id, &header.confirmed_offset,
						header.awaiting_message_count, true) != ESP_OK){
				ESP_LOGE(TAG, "Unable to fail meter values of loaded transaction.");
				goto error;
			}

			fclose(fp);
			fp = NULL;
			if(truncate(file_path, header.confirmed_offset) != 0){
				ESP_LOGE(TAG, "Unable to truncate loaded transaction file to ignore unreliable messages: %s", strerror(errno));
				goto error;
			}
		}else{
			// No more meter values can be created and current values can not be trusted. Set to read Stop message next.
			header.confirmed_offset = OFFSET_STOP_TRANSACTION;
			header.awaiting_message_count = 1;

			if(write_header(fp, &header.is_active, &header.start_timestamp, &header.transaction_id, &header.confirmed_offset,
						header.awaiting_message_count, true) != ESP_OK){
				ESP_LOGE(TAG, "Unable to fail meter values of inactive loaded transaction.");
				goto error;
			}
		}
	}else{
		ESP_LOGW(TAG, "Failing from stop");
		fclose(fp);
		fp = NULL;
		remove(file_path);
		ocpp_send_status_notification(-1, OCPP_CP_ERROR_INTERNAL_ERROR, "Error with transaction data. StopTransaction lost",
					NULL, NULL, true, false);
	}

	if(fp != NULL)
		fclose(fp);

	return;
error:
	// If we fail to fail a single message or set the file to a predictable state, then we remove the entire file
	ESP_LOGE(TAG, "Unable to correct or limit effect of failure on transaction file, Deleting whole transaction");
	if(fp != NULL)
		fclose(fp);

	if(remove(file_path) != 0){
		ESP_LOGE(TAG, "Unable to remove failed transaction: %s", strerror(errno));
	}
}

time_t ocpp_transaction_get_oldest_timestamp(){

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "File system not ready when getting oldest transaction timestamp");
		return LONG_MAX;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock when getting oldest transaction timestamp");
		return LONG_MAX;
	}

	if(loaded_transaction_data == NULL){
		load_next_transaction_message();
	}

	xSemaphoreGive(file_lock);
	return loaded_transaction_timestamp;
}

esp_err_t ocpp_transaction_load_cb_data(struct ocpp_transaction_start_stop_cb_data * cb_data_out){

	if(cb_data_out == NULL){
		ESP_LOGE(TAG, "No callback data to populate");
		return ESP_ERR_INVALID_ARG;
	}

	if(loaded_transaction_data == NULL || loaded_transaction_entry == -1){
		ESP_LOGW(TAG, "Callback data requested, but no loaded transaction exists, possibly meter value sent with queue.");
		return ESP_ERR_INVALID_STATE;
	}

	cb_data_out->transaction_entry = loaded_transaction_entry;

	if(loaded_transaction_type == eTRANSACTION_TYPE_METER){
		return ESP_ERR_NOT_FOUND; // No callback data needed/exist for meter value

	}else if(loaded_transaction_type == eTRANSACTION_TYPE_START){
		struct start_transaction_data * start_data = (struct start_transaction_data *)loaded_transaction_data;
		strcpy(cb_data_out->id_tag, start_data->id_tag);

		return ESP_OK;

	}else if(loaded_transaction_type == eTRANSACTION_TYPE_STOP){
		struct stop_transaction_data * stop_data = (struct stop_transaction_data *)loaded_transaction_data;
		strcpy(cb_data_out->id_tag, stop_data->id_tag);

		return ESP_OK;
	}

	return ESP_FAIL;
}

cJSON * read_next_message(){

	ESP_LOGI(TAG, "Attempting to read next transaction message");

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "File system not ready when reading next transaction message");
		return NULL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock when reading next transaction message");
		return NULL;
	}

	if(loaded_transaction_data == NULL){
		load_next_transaction_message();
		if(loaded_transaction_data == NULL){
			ESP_LOGW(TAG, "No next transaction data loaded to create next message");
			xSemaphoreGive(file_lock);
			return NULL;
		}
	}

	cJSON * message = NULL;

	if(loaded_transaction_type == eTRANSACTION_TYPE_START){

		struct start_transaction_data * data = (struct start_transaction_data *)loaded_transaction_data;
		message = ocpp_create_start_transaction_request(data->connector_id, data->id_tag, data->meter_start,
								(data->valid_reservation) ? &data->reservation_id : NULL,
								loaded_transaction_header.start_timestamp);

		if(message == NULL){
			fail_loaded_transaction();
		}
	}

	struct ocpp_meter_value_list * value_list = NULL;
	while(loaded_transaction_data != NULL && loaded_transaction_type == eTRANSACTION_TYPE_METER && message == NULL){

		unsigned char * data = (unsigned char *)loaded_transaction_data;
		bool is_stop_txn_data;
		value_list = ocpp_meter_list_from_contiguous_buffer(data, loaded_transaction_size, &is_stop_txn_data);

		if(value_list == NULL){
			ESP_LOGE(TAG, "Unable to create value list from meter data");
			fail_loaded_transaction();
		}else{
			message = ocpp_create_meter_values_request(1, &loaded_transaction_header.transaction_id, value_list);

			if(message == NULL){
				ESP_LOGE(TAG, "unable to create meter value request");
				fail_loaded_transaction();
			}
		}

		ocpp_meter_list_delete(value_list);
	}

	if(loaded_transaction_data != NULL && loaded_transaction_type == eTRANSACTION_TYPE_STOP){

		struct stop_transaction_data * data = (struct stop_transaction_data *)loaded_transaction_data;

		value_list = NULL;
		if(loaded_transaction_stop_meter_data != NULL){
			ESP_LOGI(TAG, "Read meter value is stop transaction data, Attempting conversion");

			unsigned char * meter_data = (unsigned char *)loaded_transaction_stop_meter_data;
			bool is_stop_txn_data;
			value_list = ocpp_meter_list_from_contiguous_buffer(meter_data, loaded_transaction_size, &is_stop_txn_data);

			if(value_list == NULL)
				ESP_LOGE(TAG, "Unable to convert StopTransaction meter data");
		}

		message = ocpp_create_stop_transaction_request(data->token_is_valid ? data->id_tag : NULL, data->meter_stop,
													data->timestamp, loaded_transaction_header.transaction_id,
													ocpp_reason_from_id(data->reason_id), value_list);

		free(value_list);

		if(message == NULL){
			ESP_LOGE(TAG, "Unable to create stop transaction");
			fail_loaded_transaction();
		}
	}

	xSemaphoreGive(file_lock);

	return message;
}


BaseType_t ocpp_transaction_get_next_message(struct ocpp_active_call * call){
	ESP_LOGI(TAG, "Getting next transaction message");

	struct ocpp_call_with_cb * call_with_cb = NULL;

	if(ocpp_transaction_get_oldest_timestamp() < peek_txn_enqueue_timestamp()){
		ESP_LOGI(TAG, "Getting message from storage");

		call_with_cb = malloc(sizeof(struct ocpp_call_with_cb));
		if(call_with_cb == NULL){
			ESP_LOGE(TAG, "Unable to allocate call for transaction message on file");
			return pdFALSE;
		}

		call_with_cb->call_message = read_next_message();

		if(check_call_with_cb_validity(call_with_cb) == false){
			ESP_LOGE(TAG, "Invalid message on file");

			free(call_with_cb->call_message);
			call_with_cb->call_message = NULL;

			fail_loaded_transaction();

			return pdFALSE;
		}else{
			ESP_LOGI(TAG, "Valid message on file");

			const char * action = ocppj_get_action_from_call(call_with_cb->call_message);
			if(strcmp(action, OCPPJ_ACTION_START_TRANSACTION) == 0){
				call_with_cb->result_cb = start_result_cb;
				call_with_cb->error_cb = start_error_cb;

			}else if(strcmp(action, OCPPJ_ACTION_STOP_TRANSACTION) == 0){
				call_with_cb->result_cb = stop_result_cb;
				call_with_cb->error_cb = stop_error_cb;

			}else if(strcmp(action, OCPPJ_ACTION_METER_VALUES) == 0){
				call_with_cb->result_cb = meter_result_cb;
				call_with_cb->error_cb = meter_error_cb;

			}else{
				ESP_LOGE(TAG, "Non enqueued transaction message is not a know transaction related message. No callback will be used");
				call_with_cb->result_cb = NULL;
				call_with_cb->error_cb = NULL;
			}

			call_with_cb->cb_data = NULL;

			call->call = call_with_cb;
			call->is_transaction_related = true;

			last_from_file = true;
			return pdTRUE;
		}

	}else if(ocpp_transaction_call_queue != NULL && uxQueueMessagesWaiting(ocpp_transaction_call_queue) > 0){
		ESP_LOGI(TAG, "Getting message from queue");

		if(xQueueReceive(ocpp_transaction_call_queue, &call->call, pdMS_TO_TICKS(1000)) == pdTRUE){
			known_message_count--;
			call->is_transaction_related = true;
			get_txn_enqueue_timestamp();
			last_from_file = false;
			return pdTRUE;
		}
	}else{
		ESP_LOGE(TAG, "No transaction message found, known transaction message count maybe incorrect");
		known_message_count = -1;
	}

	return pdFALSE;
}

esp_err_t ocpp_transaction_confirm_last(){
	if(!last_from_file){
		return ESP_OK;
	}

	if(loaded_transaction_entry == -1){
		ESP_LOGE(TAG, "No entry for loaded transaction that was confirmed");
		return ESP_ERR_INVALID_STATE;
	}

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready to confirm last transaction");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Unable to take lock to confirm last transaction message");
		return -1;
	}

	esp_err_t ret = ESP_FAIL;

	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, loaded_transaction_entry);

	long new_offset = -1;

	if(loaded_transaction_type == eTRANSACTION_TYPE_START){
		new_offset = OFFSET_METER_VALUES;

	}else if(loaded_transaction_type == eTRANSACTION_TYPE_METER){
		new_offset = loaded_transaction_on_confirmed_offset;

	}else{// eTRANSACTION_TYPE_STOP
		if(remove(file_path) != 0){
			ESP_LOGE(TAG, "Failed to remove finished transaction file '%s': %s", file_path, strerror(errno));
			ret = ESP_FAIL;
		}else{
			ESP_LOGI(TAG, "Successfully finished transaction file");
			if(known_message_count >= 0)
				known_message_count--;
			ret = ESP_OK;
		}

		goto cleanup;
	}

	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open loaded entry file");
		goto cleanup;
	}

	ret = write_header(fp, NULL, NULL, NULL, &new_offset, -1, false);
	fclose(fp);

	if(ret != ESP_OK){
		ESP_LOGE(TAG, "Unable to write new offset for last confirmed transaction");
	}

cleanup:
	free(loaded_transaction_data);
	loaded_transaction_data = NULL;

	xSemaphoreGive(file_lock);
	return ret;
}

esp_err_t ocpp_transaction_load_into_session(time_t * transaction_start_out, ocpp_id_token stored_token_out, int * transaction_id_out, bool * transaction_id_valid_out){

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready to load transaction");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to write start transaction");
		return ESP_FAIL;
	}

	struct transaction_header header;
	struct start_transaction_data start_message;

	active_entry = find_active_entry(1);
	if(active_entry == -1){
		ESP_LOGW(TAG, "No active transaction found to load into session");
		xSemaphoreGive(file_lock);
		return ESP_ERR_NOT_FOUND;
	}

	esp_err_t err;
	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, active_entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);

	FILE * fp = fopen(file_path, "rb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open active transaction file at '%s' to load into session: %s", file_path, strerror(errno));
		xSemaphoreGive(file_lock);
		return ESP_FAIL;
	}

	if(read_header(fp, &header) != ESP_OK
		|| read_start_transaction(fp, &start_message.connector_id, start_message.id_tag, &start_message.meter_start, &start_message.reservation_id, &start_message.valid_reservation, &header.start_timestamp) != ESP_OK){

		ESP_LOGE(TAG, "Unable to read session data from transaction");
		err = ESP_FAIL;
	}else{

		*transaction_start_out = header.start_timestamp;
		*transaction_id_out = header.transaction_id;
		strcpy(stored_token_out, start_message.id_tag);
		*transaction_id_valid_out = (header.confirmed_offset > OFFSET_START_TRANSACTION);

		err = ESP_OK;
	}

	fclose(fp);
	xSemaphoreGive(file_lock);

 	return err;
}

esp_err_t transaction_write_start(int connector_id, const char * id_tag, int meter_start, int * reservation_id, time_t timestamp, int * entry_nr_out){
	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready for start transaction");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to write start transaction");
		return ESP_FAIL;
	}

	esp_err_t ret = ESP_FAIL;

	*entry_nr_out = find_next_vacant_entry();
	if(*entry_nr_out != -1){
		char file_path[32];
		sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, *entry_nr_out % CONFIG_OCPP_MAX_TRANSACTION_FILES);

		ESP_LOGI(TAG, "Creating new transaction file at '%s'", file_path);

		FILE * fp = fopen(file_path, "wb+");
		if(fp == NULL){
			ESP_LOGE(TAG, "Unable to create transaction file: %s", strerror(errno));
			xSemaphoreGive(file_lock);
			return ESP_FAIL;
		}

		ret = write_start_transaction(fp, connector_id, id_tag, meter_start,
					(reservation_id != NULL) ? *reservation_id : -1, (reservation_id != NULL) ? true : false, timestamp);
		fclose(fp);
	}

	xSemaphoreGive(file_lock);
 	return ret;
}

int ocpp_transaction_enqueue_start(int connector_id, const char * id_tag, int meter_start, int * reservation_id, time_t timestamp, int * entry_out){

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready for stop transaction");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to check for active transaction when enqueueing start");
		return ESP_FAIL;
	}

	int existing_active_entry = find_active_entry(connector_id);
	xSemaphoreGive(file_lock);

	if(existing_active_entry != -1){
		ESP_LOGE(TAG, "Attempt to start transaction while other transaction is active. Should not be possible. Ending active transaction");
		ocpp_transaction_enqueue_stop(NULL, meter_start, timestamp, eOCPP_REASON_OTHER, NULL);
	}

	int ret;
	if(transaction_write_start(connector_id, id_tag, meter_start, reservation_id, timestamp, entry_out) != ESP_OK){
		ESP_LOGE(TAG, "Unable to store start transaction");
		ret = -1;
	}else{
		ret = 0;
	}

	if(task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

	return ret;
}


esp_err_t ocpp_transaction_write_stop_no_lock(const char * id_tag, int meter_stop, time_t timestamp, enum ocpp_reason_id reason){

	int entry = find_active_entry(1);
	if(entry == -1){
		ESP_LOGE(TAG, "Unable to find active transaction to write stop transaction");

		xSemaphoreGive(file_lock);
		return ESP_ERR_NOT_FOUND;
	}

	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);

	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open active transaction to write stop");

		xSemaphoreGive(file_lock);
		return ESP_FAIL;
	}

	esp_err_t err = write_stop_transaction(fp, id_tag, meter_stop, timestamp, reason);

	fclose(fp);

	return err;
}

esp_err_t ocpp_transaction_write_meter_value(const unsigned char * meter_buffer, size_t buffer_length, bool stop_related){

	if(!stop_related){
		if(!filesystem_is_ready()){
			ESP_LOGE(TAG, "Filesystem not ready for meter value");
			return ESP_FAIL;
		}

		if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
		{
			ESP_LOGE(TAG, "Failed to obtain lock to write meter value");
			return ESP_FAIL;
		}
	}

	int entry = find_active_entry(1);
	if(entry == -1){
		ESP_LOGE(TAG, "Unable to find active transaction to write meter value");
		xSemaphoreGive(file_lock);
		return ESP_ERR_NOT_FOUND;
	}

	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);

	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open active transaction to write meter value");
		xSemaphoreGive(file_lock);
		return ESP_FAIL;
	}

	if(fseek(fp, 0, SEEK_END) != 0){
		ESP_LOGE(TAG, "Unable to seek to end to write meter value");
		xSemaphoreGive(file_lock);
		return ESP_FAIL;
	}

	long offset_end = ftell(fp);
	if(offset_end == -1){
		ESP_LOGE(TAG, "Unable to get the position of file end to write meter value");
		return ESP_FAIL;
	}

	if(offset_end < OFFSET_METER_VALUES){
		if(fseek(fp, OFFSET_METER_VALUES, SEEK_SET) != 0){
			ESP_LOGE(TAG, "Unable to seek to end to write first meter value");
			xSemaphoreGive(file_lock);
			return ESP_FAIL;
		}
	}

	esp_err_t err = write_meter_value_string(fp, meter_buffer, buffer_length, time(NULL), stop_related);

	fclose(fp);

	if(!stop_related)
		xSemaphoreGive(file_lock);

	if(task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

	return err;
}

int ocpp_transaction_enqueue_meter_value(unsigned int connector_id, const int * transaction_id, struct ocpp_meter_value_list * meter_values){

	// We create request to validate that the data is valid
	cJSON * request = ocpp_create_meter_values_request(connector_id, transaction_id, meter_values);
	if(request == NULL){
		ESP_LOGE(TAG, "Unable to create transaction related meter value request");
		return -1;
	}

	int err;

	if(transaction_id != NULL){
		err = enqueue_call_immediate(request, NULL, NULL, "Meter value", eOCPP_CALL_TRANSACTION_RELATED);
	}else{
		// We don't want to enqueue transaction messages with invalid id. Instead we write to transaction file
		err = -1; // ESP_ERR_INVALID_STATE
	}

	if(err != 0){

		ESP_LOGW(TAG, "Unable to enqueue transaction related meter values");
		cJSON_Delete(request);

		ESP_LOGI(TAG, "Storing meter value on file");

		size_t meter_buffer_length;
		unsigned char * meter_buffer = ocpp_meter_list_to_contiguous_buffer(meter_values, false, &meter_buffer_length);
		if(meter_buffer == NULL){
			ESP_LOGE(TAG, "Could not create meter value as string");
			err = -1;
		}else{
			if(ocpp_transaction_write_meter_value(meter_buffer, meter_buffer_length, false)!= ESP_OK){
				ESP_LOGE(TAG, "Unable to store transaction related meter value");
				err = -1;
			}else{
				err = 0;
			}
			free(meter_buffer);
		}
	}

	return err;
}

int ocpp_transaction_enqueue_stop(const char * id_tag, int meter_stop, time_t timestamp, enum ocpp_reason_id reason, struct ocpp_meter_value_list * transaction_data){

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready for stop transaction");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to write stop transaction");
		return ESP_FAIL;
	}

	if(transaction_data != NULL){
		size_t meter_buffer_length;
		unsigned char * meter_buffer = ocpp_meter_list_to_contiguous_buffer(transaction_data, true, &meter_buffer_length);
		if(meter_buffer == NULL){
			ESP_LOGE(TAG, "Unable to create meter value as string for stop transaction request");
		}else{
			ESP_LOGI(TAG, "Storing stop transaction data as short string");
			if(ocpp_transaction_write_meter_value(meter_buffer, meter_buffer_length, true) != ESP_OK){
				ESP_LOGE(TAG, "Unable to save stop transaction data");
			}
			free(meter_buffer);
		}
	}

	if(ocpp_transaction_write_stop_no_lock(id_tag, meter_stop, timestamp, reason)){
		ESP_LOGE(TAG, "Unable to store stop transaction.");
	}

	xSemaphoreGive(file_lock);

	if(task_to_notify != NULL)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

	return 0;
}

esp_err_t ocpp_transaction_set_real_id(int entry, int new_transaction_id){

	if(entry < 0 || entry > CONFIG_OCPP_MAX_TRANSACTION_FILES){
		ESP_LOGE(TAG, "Invalid entry when attempting to set real id");
		return ESP_ERR_INVALID_ARG;
	}

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready for new transaction id");
		return ESP_FAIL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to write new transaction id");
		return ESP_FAIL;
	}

	char file_path[32];
	sprintf(file_path, "%s/%d.bin", DIRECTORY_PATH, entry % CONFIG_OCPP_MAX_TRANSACTION_FILES);

	FILE * fp = fopen(file_path, "rb+");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open transaction entry to write new transaction id");
		xSemaphoreGive(file_lock);
		return ESP_FAIL;
	}

	esp_err_t err = write_header(fp, NULL, NULL, &new_transaction_id, NULL, 0, false);

	fclose(fp);
	xSemaphoreGive(file_lock);

	return err;
}

bool remove_transaction_file(FILE * fp, const char * file_path, int entry, void * buffer){

	if(remove(file_path) != 0){
		ESP_LOGE(TAG, "Remove failed for transaction file: %s", file_path);
		*((int *)buffer) += 1;
	}else{
		ESP_LOGI(TAG, "Remove succeeded for transaction file: %s", file_path);
	}

	return true;
}

int ocpp_transaction_clear_all(){
	ESP_LOGW(TAG, "Deleting all files (ocpp)");

	if(!filesystem_is_ready()){
		ESP_LOGE(TAG, "Filesystem not ready to clear transaction info");
		return -1;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to obtain lock to clear transaction info");
		return -1;
	}

	if(ocpp_transaction_call_queue != NULL)
		xQueueReset(ocpp_transaction_call_queue);

	int failed_removal_count = 0;
	if(foreach_transaction_file(remove_transaction_file, &failed_removal_count, false) != ESP_OK){
		ESP_LOGE(TAG, "Unable to loop over transaction file to clear all");
	}

	loaded_transaction_reset();
	known_message_count = -1;

	xSemaphoreGive(file_lock);

	ESP_LOGI(TAG, "clear all transaction complete. Failed to remove %d files", failed_removal_count);
	return failed_removal_count;
}

void ocpp_transaction_configure_task_notification(TaskHandle_t task, uint offset){
	task_to_notify = task;
	task_notify_offset = offset;
}

int ocpp_transaction_init(){
	ESP_LOGI(TAG, "Initializing ocpp transaction with storage");

	if(filesystem_is_ready()){
		ESP_LOGW(TAG, "Already initialized");
		if(task_to_notify != NULL && ocpp_transaction_get_oldest_timestamp() < LONG_MAX)
			xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

		return 0;
	}

	known_message_count = -1;

	struct stat st;
	if(stat(CONFIG_OCPP_FILE_PATH, &st) != 0){
		ESP_LOGE(TAG, "OCPP file path ('%s') not mounted", CONFIG_OCPP_FILE_PATH);
		return -1;
	}

	SemaphoreHandle_t initial_lock = xSemaphoreCreateMutex();
	if(initial_lock == NULL){
		ESP_LOGE(TAG, "Unable to create file lock");
		goto error;
	}

	ocpp_transaction_call_queue = xQueueCreate(CONFIG_OCPP_MAX_TRANSACTION_QUEUE_SIZE, sizeof(struct ocpp_call_with_cb *));
	if(ocpp_transaction_call_queue == NULL){
		ESP_LOGE(TAG, "Unable to create transaction call queue");
		return -1;
	}

	if(stat(DIRECTORY_PATH, &st) != 0){
		ESP_LOGI(TAG, "Creating direcory path '%s'", DIRECTORY_PATH);
		if(mkdir(DIRECTORY_PATH, S_IRWXU | S_IRWXG) != 0){
			ESP_LOGE(TAG, "Unable to create offline transaction directory: '%s'", strerror(errno));
			goto error;
		}
	}else{
		ESP_LOGI(TAG, "Directory path '%s' exists", DIRECTORY_PATH);
	}

	xSemaphoreGive(initial_lock);
	file_lock = initial_lock;

	if(task_to_notify != NULL && start_result_cb != NULL && ocpp_transaction_get_oldest_timestamp() < LONG_MAX)
		xTaskNotify(task_to_notify, eOCPP_TASK_CALL_ENQUEUED << task_notify_offset, eSetBits);

	return 0;

error:
	if(ocpp_transaction_call_queue != NULL){
		vQueueDelete(ocpp_transaction_call_queue);
		ocpp_transaction_call_queue = NULL;
	}

	if(initial_lock != NULL)
		vSemaphoreDelete(initial_lock);

	return -1;
}

bool ocpp_transaction_is_ready(){
	return filesystem_is_ready();
}

void ocpp_transaction_deinit(){
	ESP_LOGI(TAG, "Deiniting ocpp transaction with storage");

	if(file_lock != NULL){
		vSemaphoreDelete(file_lock);
		file_lock = NULL;
	}

	if(ocpp_transaction_call_queue != NULL){
		vQueueDelete(ocpp_transaction_call_queue);
		ocpp_transaction_call_queue = NULL;
	}

	free(loaded_transaction_data);
	loaded_transaction_data = NULL;
};

void ocpp_transaction_fail_all(const char * error_description){
	struct ocpp_active_call call = {0};
	while(ocpp_transaction_get_next_message(&call) != pdFALSE){
		call.retries = UINT_MAX;
		fail_active_call(&call, OCPPJ_ERROR_INTERNAL, error_description, NULL);
	}
}
