#ifndef OCPP_OTA_H
#define OCPP_OTA_H

#include "esp_https_ota.h"

void do_ocpp_ota_abort();
void do_ocpp_ota(char *image_location, void (*status_update_cb)(const char * status));
void ota_set_ocpp_request_size(size_t request_size);

#endif /* OCPP_OTA_H */
