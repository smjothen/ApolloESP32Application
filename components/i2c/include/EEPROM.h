
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t EEPROM_Read();
esp_err_t EEPROM_Write();

esp_err_t EEPROM_ReadFormatVersion(uint8_t * formatVersionToRead);
esp_err_t EEPROM_WriteFormatVersion(uint8_t formatVersionToWrite);

esp_err_t EEPROM_ReadFactroyStage(uint8_t *factory_stage);
esp_err_t EEPROM_WriteFactoryStage(uint8_t stage);

esp_err_t EEPROM_ReadSerialNumber(char * serianNumberToRead);
esp_err_t EEPROM_WriteSerialNumber(char * serialNumberToWrite);

esp_err_t EEPROM_ReadPSK(char * PSKToRead);
esp_err_t EEPROM_WritePSK(char * PSKToWrite);

esp_err_t EEPROM_ReadPin(char * pinToRead);
esp_err_t EEPROM_WritePin(char * pinToWrite);

void EEPROM_WriteFullTest();
void EEPROM_Erase();

#ifdef __cplusplus
}
#endif
