#include "DeviceInfo.h"
#include "offline_log.h"

#define TAG "OFFLINE_LOG    "

#include "esp_crc.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#include "errno.h"

#include "OCMF.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_protocol_serialisation.h"

#define OFFLINE_LOG_MAX_ITEMS 1000
#define OFFLINE_LOG_FLAG_64BIT (1 << 0)

struct LogFile {
	SemaphoreHandle_t lock;
	const char *mount;
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
#ifdef CONFIG_ZAPTEC_GO_PLUS
	// Go+ stores position of tariff value in MID log
	uint32_t pos;
#else
	time_t timestamp;
	double energy;
#endif
	uint32_t crc;
};

static struct LogFile log = {
	.lock = NULL,
	.mount = "/files",
	.path = "/files/log64.bin",
	.flags = OFFLINE_LOG_FLAG_64BIT,
	.entry_size = sizeof(struct LogLine),
};

int update_header(FILE *fp, int start, int end) {
	struct LogHeader new_header = { .start = start, .end = end, .crc = 0 };
	uint32_t crc = esp_crc32_le(0, (uint8_t *)&new_header, sizeof(new_header));
	new_header.crc = crc;
	fseek(fp, 0, SEEK_SET);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	int write_result = fwrite(&new_header, 1, sizeof(new_header), fp);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	ESP_LOGI(TAG, "writing header %d %d %" PRIu32 " (s:%d, res:%d)    <<<<   ",
			 new_header.start, new_header.end, new_header.crc,
			 sizeof(new_header), write_result);

	if (write_result != sizeof(new_header)) {
		return -1;
	}

	return 0;
}

int ensure_valid_header(FILE *fp, int *start_out, int *end_out) {
	struct LogHeader head_in_file = { 0 };
	fseek(fp, 0, SEEK_SET);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	int read_result = fread(&head_in_file, 1, sizeof(head_in_file), fp);
	ESP_LOGI(TAG, "header on disk %d %d %" PRIu32 " (s:%d, res:%d)    <<<   ",
			 head_in_file.start, head_in_file.end, head_in_file.crc,
			 sizeof(head_in_file), read_result);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	if (read_result < sizeof(head_in_file)) {
		ESP_LOGE(TAG, "fread errno %d: %s", errno, strerror(errno));
	}
	uint32_t crc_in_file = head_in_file.crc;
	head_in_file.crc = 0;

	uint32_t calculated_crc =
		esp_crc32_le(0, (uint8_t *)&head_in_file, sizeof(head_in_file));

	if (crc_in_file == calculated_crc) {
		ESP_LOGI(TAG, "Found valid header");
		*start_out = head_in_file.start;
		*end_out = head_in_file.end;
	} else {
		ESP_LOGE(TAG, "INVALID HEAD, staring log anew");

		int new_header_result = update_header(fp, 0, 0);
		*start_out = 0;
		*end_out = 0;

		if (new_header_result < 0) {
			return -1;
		}
	}

	return 0;
}

FILE *init_and_lock_log(const struct LogFile *file, int *start, int *end) {
	struct stat st;
	if (stat(file->mount, &st) != 0) {
		ESP_LOGE(TAG, "'%s' not mounted, offline log will not work",
				 file->mount);
		return NULL;
	}

	if (file->lock == NULL) {
		ESP_LOGE(TAG, "No allocated mutex for offline log!");
		return NULL;
	}

	if (xSemaphoreTake(file->lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to aquire mutex lock for offline log");
		return NULL;
	}

	FILE *fp = NULL;
	if (stat(file->path, &st) != 0) {
		// Doesn't exist, w+ will create
		fp = fopen(file->path, "w+b");
	} else {
		// Does exist, r+ will allow updating
		fp = fopen(file->path, "r+b");
	}

	if (fp == NULL) {
		ESP_LOGE(TAG, "failed to open file ");
		xSemaphoreGive(file->lock);
		return NULL;
	}

	size_t log_size =
		sizeof(struct LogHeader) + file->entry_size * OFFLINE_LOG_MAX_ITEMS;

	int seek_res = fseek(fp, log_size, SEEK_SET);

	if (seek_res < 0) {
		xSemaphoreGive(file->lock);
		return NULL;
	}
	ESP_LOGI(TAG, "expanded log to %d(%d)", log_size, seek_res);

	fseek(fp, 0, SEEK_SET);

	if (ensure_valid_header(fp, start, end) < 0) {
		ESP_LOGE(TAG, "failed to create log header");
		xSemaphoreGive(file->lock);
		return NULL;
	}

	return fp;
}

int release_log(struct LogFile *file, FILE *fp) {
	int result = 0;
	if (fp != NULL && fclose(fp) != 0) {
		result = EOF;
		ESP_LOGE(TAG, "Unable to close offline log: %s", strerror(errno));
	}

	if (xSemaphoreGive(file->lock) !=
		pdTRUE) // "indicating that the semaphore was not first obtained correctly"
		ESP_LOGE(TAG, "Unable to give log mutex");

	return result;
}

#ifdef CONFIG_ZAPTEC_GO_PLUS
void offline_log_append_energy(uint32_t pos) {
	ESP_LOGI(TAG, "saving offline energy MID %" PRIu32, pos);
#else
void offline_log_append_energy(time_t timestamp, double energy) {
	ESP_LOGI(TAG, "saving offline energy %fWh@%lld", energy, timestamp);
#endif

	int log_start;
	int log_end;

	// Always append to 64bit path for future energy
	FILE *fp = init_and_lock_log(&log, &log_start, &log_end);

	if (fp == NULL)
		return;

	int new_log_end = (log_end + 1) % OFFLINE_LOG_MAX_ITEMS;
	if (new_log_end == log_start) {
		// insertion would fill the buffer, and cant tell if it is full or empty
		// move first item idx to compensate
		log_start = (log_start + 1) % OFFLINE_LOG_MAX_ITEMS;
	}

#ifdef CONFIG_ZAPTEC_GO_PLUS
	struct LogLine line = { .pos = pos,
							.crc = 0 };
#else
	struct LogLine line = { .energy = energy,
							.timestamp = timestamp,
							.crc = 0 };
#endif

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&line, sizeof(line));
	line.crc = crc;

	ESP_LOGI(TAG, "writing to file with crc=%" PRIu32 "", line.crc);

	int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_end);
	fseek(fp, start_of_line, SEEK_SET);
	int bytes_written = fwrite(&line, 1, sizeof(line), fp);

	update_header(fp, log_start, new_log_end);
	ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
			 bytes_written, start_of_line, log_start, new_log_end);

	release_log(&log, fp);
}

int _offline_log_attempt_send(struct LogFile *file) {
	ESP_LOGI(TAG, "log data (%s):", file->path);

	int result = -1;
	int log_start;
	int log_end;
	FILE *fp = init_and_lock_log(file, &log_start, &log_end);

	//If file or partition is now available, indicate empty file
	if (fp == NULL)
		return 0;

	char ocmf_text[200] = { 0 };

	while (log_start != log_end) {
		int read_result, start_of_line;
		uint32_t crc_on_file, calculated_crc;

		struct LogLine line;
		start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_start);

		fseek(fp, start_of_line, SEEK_SET);
		read_result = fread(&line, 1, sizeof(line), fp);

		crc_on_file = line.crc;
		line.crc = 0;
		calculated_crc = esp_crc32_le(0, (uint8_t *)&line, sizeof(line));

#ifdef CONFIG_ZAPTEC_GO_PLUS
		uint32_t mid_id = line.pos;

		ESP_LOGI(TAG,
				 "LogLine%d@%d>%d: P=%" PRIu32 " crc=%" PRId32
				 ", valid=%d, read=%d",
				 (file->flags & OFFLINE_LOG_FLAG_64BIT) ? 64 : 32, log_start,
				 start_of_line, mid_id, crc_on_file,
				 crc_on_file == calculated_crc, read_result);
#else
		double energy = line.energy;
		time_t timestamp = line.timestamp;

		ESP_LOGI(TAG,
				 "LogLine%d@%d>%d: E=%f, t=%lld, crc=%" PRId32
				 ", valid=%d, read=%d",
				 (file->flags & OFFLINE_LOG_FLAG_64BIT) ? 64 : 32, log_start,
				 start_of_line, energy, timestamp, crc_on_file,
				 crc_on_file == calculated_crc, read_result);
#endif

		if (crc_on_file == calculated_crc) {

#ifdef CONFIG_ZAPTEC_GO_PLUS
			OCMF_SignedMeterValue_CreateMessageFromMID(ocmf_text, sizeof (ocmf_text), mid_id, false);
#else
			OCMF_SignedMeterValue_CreateMessageFromLog(ocmf_text, timestamp,
													   energy);
#endif

			int publish_result = publish_string_observation_blocked(
				SignedMeterValue, ocmf_text, 2000);

			if (publish_result < 0) {
				ESP_LOGI(TAG, "publishing line failed, aborting log dump");
				break;
			}

			int new_log_start = (log_start + 1) % OFFLINE_LOG_MAX_ITEMS;
			update_header(fp, new_log_start, log_end);
			fflush(fp);

			ESP_LOGI(TAG, "line published");
		} else {
			ESP_LOGI(TAG, "skipped corrupt line");
		}

		log_start = (log_start + 1) % OFFLINE_LOG_MAX_ITEMS;
	}

	if (log_start == log_end) {
		result = 0;
		if (log_start != 0)
			update_header(fp, 0, 0);
	}

	release_log(file, fp);
	return result;
}

int offline_log_attempt_send(void) {
	// Always attempt to send 64-bit log
	int ret = _offline_log_attempt_send(&log);
	if (ret != 0) {
		ESP_LOGE(TAG, "Attempted send of log failed!");
		return ret;
	}

	ESP_LOGI(TAG, "Attempted send of log successful!");
	return ret;
}

int offline_log_delete(void) {
	struct stat st;
	if (stat(log.mount, &st) != 0) {
		ESP_LOGE(TAG, "'%s' not mounted. Unable to delete offline log",
				 log.mount);
		return ENOTDIR;
	}

	if (log.lock == NULL) {
		return ENOLCK;
	}

	if (xSemaphoreTake(log.lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to aquire mutex lock to delete offline log");
		return ETIMEDOUT;
	}

	FILE *fp = fopen(log.path, "r");

	if (fp == NULL) {
		ESP_LOGE(TAG, "Before remove: logfile %s can't be opened ", log.path);
	} else {
		ESP_LOGE(TAG, "Before remove: logfile %s can be opened ", log.path);
		fclose(fp);
	}

	remove(log.path);
	int ret;

	fp = fopen(log.path, "r");
	if (fp == NULL) {
		ESP_LOGE(TAG, "After remove: logfile %s can't be opened ", log.path);
		ret = 1;
	} else {
		ESP_LOGE(TAG, "After remove: logfile %s can be opened ", log.path);
		fclose(fp);
		ret = 2;
	}

	return ret;
}

void offline_log_init(void) {
	log.lock = xSemaphoreCreateMutex();
	if (log.lock == NULL) {
		ESP_LOGE(TAG, "Failed to create mutex for offline log");
		return;
	}
}
