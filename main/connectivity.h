#ifndef _CONNECTIVITY_H_
#define _CONNECTIVITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DeviceInfo.h"

void connectivity_init();
void connectivityActivateInterface(enum ConnectionInterface selectedInterface);


#ifdef __cplusplus
}
#endif

#endif  /*_CONNECTIVITY_H_*/
