#include <stdio.h>
#include <string.h>
#include "errno.h"

#include "esp_crc.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#include "mid_log.h"
#include "mid_ocmf.h"

#define MIDLOG_MAX_ITEMS 1000
#define MIDLOG_MOUNT "/files"
#define MIDLOG_PATH "/files/log.bin"

static const char *TAG = "OFFLINE_LOG    ";

typedef struct _midlog_ctx_t {
	SemaphoreHandle_t lock;
	midlog_pub_t pub_handler;
	midlog_energy_t energy_handler;
} midlog_ctx_t;

typedef struct {
    int start;
    int end;
    uint32_t crc; // for header, not whole file
    // dont keep version, use other file name for versioning
} midlog_header_t;

typedef struct {
    time_t timestamp;
    uint32_t energy;
    uint32_t crc;
	// TODO: Just use midsess_meter_val_t? which has versions
} midlog_item_t;

static int update_header(FILE *fp, int start, int end) {
	midlog_header_t new_header = {.start=start, .end=end, .crc=0};

	uint32_t crc =  esp_crc32_le(0, (uint8_t *)&new_header, sizeof (new_header));
	new_header.crc = crc;

	fseek(fp, 0, SEEK_SET);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));

	int write_result = fwrite(&new_header, 1,  sizeof (new_header), fp);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	ESP_LOGI(TAG, "writing header %d %d %" PRIu32 " (s:%d, res:%d)    <<<<   ", new_header.start, new_header.end, new_header.crc, sizeof (new_header), write_result);

	if (write_result != sizeof (new_header)) {
		return -1;
	}

	return 0;
}

static int ensure_valid_header(FILE *fp, int *start_out, int *end_out) {
	midlog_header_t head_in_file = {0};

	fseek(fp, 0, SEEK_SET);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));
	int read_result = fread(&head_in_file, 1, sizeof (head_in_file),  fp);
	ESP_LOGI(TAG, "header on disk %d %d %" PRIu32 " (s:%d, res:%d)    <<<   ",
	head_in_file.start, head_in_file.end, head_in_file.crc, sizeof (head_in_file), read_result);
	ESP_LOGI(TAG, "file error %d eof %d", ferror(fp), feof(fp));

	if (read_result < sizeof (head_in_file)) {
		ESP_LOGE(TAG, "fread errno %d: %s", errno, strerror(errno));
	}

	uint32_t crc_in_file = head_in_file.crc;
	head_in_file.crc = 0;

	uint32_t calculated_crc = esp_crc32_le(0, (uint8_t *)&head_in_file, sizeof (head_in_file));

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

static FILE * init_and_lock_log(midlog_ctx_t *ctx, int *start, int *end) {
	struct stat st;
	if (stat(MIDLOG_MOUNT, &st) != 0) {
		ESP_LOGE(TAG, "'%s' not mounted, offline log will not work", MIDLOG_MOUNT);
		return NULL;
	}

	if (ctx->lock == NULL) {
		ESP_LOGE(TAG, "No allocated mutex for offline log!");
		return NULL;
	}

	if (xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to aquire mutex lock for offline log");
		return NULL;
	}

	FILE *fp = NULL;
	if (stat(MIDLOG_PATH, &st) != 0) {
		// Doesn't exist, w+ will create
		fp = fopen(MIDLOG_PATH, "w+b");
	} else {
		// Does exist, r+ will allow updating
		fp = fopen(MIDLOG_PATH, "r+b");
	}

	if (fp == NULL) {
		ESP_LOGE(TAG, "failed to open file ");
		xSemaphoreGive(ctx->lock);
		return NULL;
	}

	size_t log_size = sizeof (midlog_header_t) + sizeof (midlog_item_t) * MIDLOG_MAX_ITEMS;

	int seek_res = fseek(fp, log_size, SEEK_SET);
	if (seek_res < 0) {
		xSemaphoreGive(ctx->lock);
		return NULL;
	}

	ESP_LOGI(TAG, "expanded log to %d(%d)", log_size, seek_res);

	fseek(fp, 0, SEEK_SET);

	if (ensure_valid_header(fp, start, end) < 0) {
		ESP_LOGE(TAG, "failed to create log header");
		xSemaphoreGive(ctx->lock);
		return NULL;
	}

	return fp;
}

static int release_log(midlog_ctx_t *ctx, FILE *fp) {
	int result = 0;
	if (fp != NULL && fclose(fp) != 0) {
		result = EOF;
		ESP_LOGE(TAG, "Unable to close offline log: %s", strerror(errno));
	}

	if (xSemaphoreGive(ctx->lock) != pdTRUE) { // "indicating that the semaphore was not first obtained correctly"
		ESP_LOGE(TAG, "Unable to give log mutex");
	}

	return result;
}

int midlog_append_energy_private(midlog_ctx_t *ctx, time_t timestamp, uint32_t energy) {
	ESP_LOGI(TAG, "saving offline energy %" PRId32 "Wh@%lld", energy, timestamp);

	int log_start;
	int log_end;

	// Always append to 64bit path for future energy
	FILE *fp = init_and_lock_log(ctx, &log_start, &log_end);

	if (fp == NULL) {
		return 1;
	}

	int new_log_end = (log_end+1) % MIDLOG_MAX_ITEMS;
	if (new_log_end == log_start) {
		// insertion would fill the buffer, and cant tell if it is full or empty
		// move first item idx to compensate
		log_start = (log_start+1) % MIDLOG_MAX_ITEMS;
	}

	midlog_item_t line = {.energy = energy, .timestamp = timestamp, .crc = 0};

	uint32_t crc = esp_crc32_le(0, (uint8_t *)&line, sizeof (line));
	line.crc = crc;

	ESP_LOGI(TAG, "writing to file with crc=%" PRIu32 "", line.crc);

	int start_of_line = sizeof (midlog_header_t) + (sizeof (line) * log_end);
	fseek(fp, start_of_line, SEEK_SET);
	int bytes_written = fwrite(&line, 1, sizeof (line), fp);

	update_header(fp, log_start, new_log_end);
	ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
		bytes_written, start_of_line, log_start, new_log_end
	);

	release_log(ctx, fp);
	return 0;
}

int midlog_append_energy(midlog_ctx_t *ctx) {
	// TODO: Use MID package
	uint32_t energy = 0;
	time_t timestamp = time(NULL);
	return midlog_append_energy_private(ctx, timestamp, energy);
}

static int _offline_log_read_line(FILE *fp, int index, int *line_start, uint32_t *line_energy,
        time_t *line_ts, uint32_t *line_crc, uint32_t *calc_crc) {
	*line_start = sizeof (midlog_header_t) + (sizeof (midlog_item_t) * index);
	fseek(fp, *line_start, SEEK_SET);

	midlog_item_t line;
	int res = fread(&line, 1, sizeof (line),  fp);

	*line_energy = line.energy;
	*line_ts = line.timestamp;
	*line_crc = line.crc;

	line.crc = 0;
	*calc_crc = esp_crc32_le(0, (uint8_t *)&line, sizeof (line));

	return res;
}

int midlog_attempt_send(midlog_ctx_t *ctx) {
	int result = -1;
	int log_start;
	int log_end;
	FILE *fp = init_and_lock_log(ctx, &log_start, &log_end);

	//If file or partition is now available, indicate empty file
	if (fp == NULL) {
		return 0;
	}

	char ocmf_text[256] = {0};

	while (log_start != log_end) {
		int start_of_line;
		uint32_t crc_on_file, calculated_crc;
		uint32_t energy;
		time_t timestamp;

		int read_result = _offline_log_read_line(fp, log_start, &start_of_line, &energy, &timestamp, &crc_on_file, &calculated_crc);

		ESP_LOGI(TAG, "LogLine@%d>%d: E=%" PRId32 "Wh, t=%lld, crc=%" PRId32 ", valid=%d, read=%d", log_start, start_of_line, energy, timestamp, crc_on_file, crc_on_file==calculated_crc, read_result);

		if (crc_on_file == calculated_crc) {
			// TODO: Fix
			//OCMF_SignedMeterValue_CreateMessageFromLog(ocmf_text, timestamp, energy);
			int ret = midocmf_create_fiscal_message(ocmf_text, sizeof (ocmf_text),
					"ZAPXXXYYY", "2.0.4.1", "v1.2.3", timestamp, 0, energy);
			if (ret < 0) {
				ESP_LOGE(TAG, "Failure creating OCMF message");
				break;
			}

			int publish_result = ctx->pub_handler(ocmf_text);

			if (publish_result < 0) {
				ESP_LOGI(TAG, "publishing line failed, aborting log dump");
				break;
			}

			int new_log_start = (log_start + 1) % MIDLOG_MAX_ITEMS;
			update_header(fp, new_log_start, log_end);
			fflush(fp);

			ESP_LOGI(TAG, "line published");
		} else {
			ESP_LOGI(TAG, "skipped corrupt line");
		}

		log_start = (log_start+1) % MIDLOG_MAX_ITEMS;
	}

	if (log_start == log_end) {
		result = 0;
		if (log_start != 0) {
			update_header(fp, 0, 0);
		}
	}

	release_log(ctx, fp);
	return result;
}

int midlog_init(midlog_ctx_t *ctx, midlog_pub_t publisher, midlog_energy_t energy) {
	ctx->lock = xSemaphoreCreateMutex();

	ctx->pub_handler = publisher;
	ctx->energy_handler = energy;

	if (!ctx->lock) {
		ESP_LOGE(TAG, "Failed to create mutex for offline log");
		return 1;
	}

	return 0;
}

int midlog_free(midlog_ctx_t *ctx) {
	if (ctx->lock) {
		vSemaphoreDelete(ctx->lock);
		ctx->lock = NULL;
	}
	return 0;
}

