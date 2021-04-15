#ifndef _PRODUCTIONTEST_H_
#define _PRODUCTIONTEST_H_

#include "DeviceInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

int prodtest_perform(struct DeviceInfo device_info, bool new_id);
int prodtest_getNewId(bool validate_only);

bool prodtest_active();
int prodtest_on_nfc_read();

#ifdef __cplusplus
}
#endif

#endif  /*_PRODUCTIONTEST_H_*/




