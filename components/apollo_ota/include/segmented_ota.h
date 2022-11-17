#ifndef SEGMENTED_OTA
#define SEGMENTED_OTA

#include "esp_https_ota.h"

void do_segment_ota_abort();
void do_segmented_ota(char *image_location);

#endif /* SEGMENTED_OTA */
