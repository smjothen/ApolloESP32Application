
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include "esp_err.h"

esp_err_t RTCWriteTime(struct tm newTime);
struct tm RTCReadTime();

bool RTCIsRegisterChanged();

uint32_t RTCGetValueCheckCounter0();
uint32_t RTCGetValueCheckCounter1();

uint8_t RTCGetLastIncorrectValue0();
uint8_t RTCGetLastIncorrectValue1();

uint8_t RTCGetLastValue0();
uint8_t RTCGetLastValue1();

uint8_t RTCGetBootValue0();
uint8_t RTCGetBootValue1();

void RTCSoftwareReset();
bool RTCReadAndUseTime();
void RTCTestTime();
void RTCVerifyControlRegisters();
void RTCWriteControl(uint8_t value);


#ifdef __cplusplus
}
#endif
