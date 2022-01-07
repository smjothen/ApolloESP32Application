#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

bool connectivity_GetSNTPInitialized();
bool connectivity_GetMQTTInitialized();
enum CommunicationMode connectivity_GetActivateInterface();
enum CommunicationMode connectivity_GetPreviousInterface();
void connectivity_init();
void connectivity_ActivateInterface(enum CommunicationMode selectedInterface);
int connectivity_GetStackWatermark();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
