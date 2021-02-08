#ifndef _PRODUCTIONTEST_H_
#define _PRODUCTIONTEST_H_

#include "DeviceInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

int prodtest_perform(struct DeviceInfo device_info);
int prodtest_getNewId();

bool prodtest_active();
int prodtest_on_nfc_read();

#ifdef __cplusplus
}
#endif

#endif  /*_PRODUCTIONTEST_H_*/




