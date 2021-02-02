#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

bool connectivity_GetSNTPInitialized();
enum CommunicationMode connectivity_GetActivateInterface();
void connectivity_init();
void connectivity_ActivateInterface(enum CommunicationMode selectedInterface);
int connectivity_GetStackWatermark();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
