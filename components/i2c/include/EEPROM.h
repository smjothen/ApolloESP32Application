
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t EEPROM_Read();
esp_err_t EEPROM_Write();

esp_err_t EEPROM_ReadFormatVersion(uint8_t formatVersionToRead);
int EEPROM_WriteFormatVersion(uint8_t formatVersionToWrite);

esp_err_t EEPROM_ReadSerialNumber(char * serianNumberToRead);
int EEPROM_WriteSerialNumber(char * serialNumberToWrite);

esp_err_t EEPROM_ReadPSK(char * PSKToRead);
int EEPROM_WritePSK(char * PSKToWrite);

esp_err_t EEPROM_ReadPin(char * pinToRead);
int EEPROM_WritePin(char * pinToWrite);

void EEPROM_WriteFullTest();
void EEPROM_Erase();

#ifdef __cplusplus
}
#endif
