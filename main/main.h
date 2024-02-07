#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"


#include "DeviceInfo.h"

void GetTimeOnString(char * onTimeString);
void SetOnlineWatchdog();
char * GetCapabilityString();

#ifdef __cplusplus
}
#endif

#endif  /*_MAIN_H_*/




