#ifndef SAFE_OTA
#define SAFE_OTA

#include "esp_https_ota.h"

void do_safe_ota_abort();
void do_safe_ota(char *image_location);
void ota_set_chunk_size(int newSize);

#endif /* SEGMENTED_OTA */
