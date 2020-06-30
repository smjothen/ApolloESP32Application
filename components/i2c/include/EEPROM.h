
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t EEPROM_Read();
esp_err_t EEPROM_Write();
void EEPROM_WriteFullTest();

#ifdef __cplusplus
}
#endif
