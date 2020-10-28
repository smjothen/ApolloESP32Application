#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

bool connectivity_GetSNTPInitialized();
void connectivity_init(int switchState);
void connectivity_ActivateInterface(enum ConnectionInterface selectedInterface);
int connectivity_GetStackWatermark();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
