
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include "esp_err.h"

esp_err_t RTCWriteTime(struct tm newTime);
struct tm RTCReadTime();
void RTCSoftwareReset();
bool RTCReadAndUseTime();
void RTCTestTime();


#ifdef __cplusplus
}
#endif
