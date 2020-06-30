
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t SHT30Init();
float SHT30ReadTemperature();
float SHT30ReadHumidity();

#ifdef __cplusplus
}
#endif
