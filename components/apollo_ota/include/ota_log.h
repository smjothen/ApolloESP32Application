#ifndef OTA_LOG_H
#define OTA_LOG_H

int ota_log_location_fetch();
int ota_log_download_start(char *location);
int ota_log_download_progress_debounced(uint32_t bytes_received);
int ota_log_flash_success();
int ota_log_timeout();
int ota_log_lib_error();

int ota_log_chunked_update_start(char *location);
int ota_log_chunk_flashed(uint32_t start, uint32_t end, uint32_t total);
int ota_log_chunk_flash_error(uint32_t error_code);
int ota_log_chunk_http_error(uint32_t error_code);
int ota_log_chunk_validation_error(uint32_t error_code);
int ota_log_all_chunks_success();

#endif /* OTA_LOG_H */
