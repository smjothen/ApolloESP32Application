#ifndef SAFE_OTA
#define SAFE_OTA

#include "esp_https_ota.h"

void do_safe_ota_abort();
void do_safe_ota(char *image_location);

#endif /* SEGMENTED_OTA */
