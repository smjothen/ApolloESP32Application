#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

bool connectivity_GetSNTPInitialized();
void connectivity_init(int switchState);
void connectivityActivateInterface(enum ConnectionInterface selectedInterface);


#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
