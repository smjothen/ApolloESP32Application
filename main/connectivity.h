#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

bool connectivity_GetSNTPInitialized();
bool connectivity_GetMQTTInitialized();
enum eCommunicationMode connectivity_GetActivateInterface();
enum eCommunicationMode connectivity_GetPreviousInterface();
uint32_t connectivity_GetNrOfLTEReconnects();
void connectivity_init();
void connectivity_ActivateInterface(enum eCommunicationMode selectedInterface);
int connectivity_GetStackWatermark();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
