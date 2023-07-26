#ifndef APOLLO_OTA_H
#define APOLLO_OTA_H

#include <stdbool.h>

void start_ota_task(void);
int start_ota(void);
int start_segmented_ota(void);
int start_segmented_ota_if_new_version(void);
int start_safe_ota(void);
bool otaIsRunning();
void validate_booted_image(void);
const char* OTAReadRunningPartition();
void ota_rollback();
bool ota_rollback_to_factory();
bool otaIsRunning();
bool ota_CheckIfHasBeenUpdated();
int ota_GetStackWatermark();
void ota_time_left();

#endif /* APOLLO_OTA_H */
