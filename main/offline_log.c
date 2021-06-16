#include "offline_log.h"

#define TAG "OFFLINE_LOG"

#include "esp_log.h"
#include "errno.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

const char *tmp_path = "/tmp";
const char *log_path = "/tmp/log554.bin";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static const int max_log_items = 10;

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

bool mount_tmp()
{
    static bool mounted = false;

	if(mounted)
	{
		ESP_LOGI(TAG, "/tmp already mounted");
		return mounted;
	}

    ESP_LOGI(TAG, "Mounting /tmp");
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

	ESP_LOGI(TAG, "Mounted");

	return mounted;
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
    perror("read perror: ");
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

FILE * init_log(int *start, int *end){
    bool mounted = mount_tmp();

    if(!mounted){
        ESP_LOGE(TAG, "failed to mount /tmp, offline log will not work");
        return NULL;
    }

    FILE *fp = fopen(log_path, "wb+");
    if(fp==NULL){
        ESP_LOGE(TAG, "failed to open file ");
        return NULL;
    }

    size_t log_size = sizeof(struct LogHeader) + (sizeof(struct LogLine)*max_log_items);

    int seek_res = fseek(
        fp,
        log_size, 
        SEEK_SET
    );

    if(seek_res < 0){
        ESP_LOGE(TAG, "seek failed");
        return NULL;
    }
    ESP_LOGI(TAG, "expanded log to %d(%d)", log_size, seek_res);


    putc('\0', fp);

    fseek(fp, 0, SEEK_SET);

    if(ensure_valid_header(fp, start, end)<0){
        ESP_LOGE(TAG, "failed to create log header");
        return NULL;
    }

    return fp;
}


void append_offline_energy(int timestamp, double energy){
    ESP_LOGI(TAG, "saving offline energy %fWh@%d", energy, timestamp);

    int log_start;
    int log_end;
    FILE *fp = init_log(&log_start, &log_end);

    if(fp==NULL)
        return;

    struct LogLine line = {.energy = energy, .timestamp = timestamp, .crc = 0};

    uint32_t crc = crc32_normal(0, &line, sizeof(struct LogLine));
    line.crc = crc;

    ESP_LOGI(TAG, "writing to file with crc=%u", line.crc);

    int start_of_line = sizeof(struct LogHeader) + (sizeof(line) * log_end);
    fseek(fp, start_of_line, SEEK_SET);
    int bytes_written = fwrite(&line, 1, sizeof(line), fp);
    log_end++;
    update_header(fp, log_start, log_end);
    ESP_LOGI(TAG, "wrote %d bytes @ %d; updated header to (%d, %d)",
        bytes_written, start_of_line, log_start, log_end
    );

 
    int close_result = fclose(fp);
    ESP_LOGI(TAG, "closed log file %d", close_result);
}