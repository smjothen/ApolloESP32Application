#ifndef OTA_LOG_H
#define OTA_LOG_H

int ota_log_location_fetch();
int ota_log_download_start(char *location);
int ota_log_download_progress_debounced(uint32_t bytes_received);
int ota_log_flash_success();
int ota_log_lib_error();

#endif /* OTA_LOG_H */
