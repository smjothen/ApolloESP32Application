#ifndef APOLLO_OTA_H
#define APOLLO_OTA_H

void start_ota_task(void);
int start_ota(void);
bool otaIsRunning();
void validate_booted_image(void);
const char* OTAReadRunningPartition();

#endif /* APOLLO_OTA_H */
