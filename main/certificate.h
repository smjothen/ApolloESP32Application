#ifndef _CERTIFICATE_H_
#define _CERTIFICATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void certificate_SetUsage(bool usage);
bool certificate_GetUsage();
bool certificateValidate();
void certificate_init();
bool certificateOk();
void certificate_clear();
void certificate_update(int tls_error);
void certifcate_setBundleVersion(int newBundleVersion);
int certificate_GetCurrentBundleVersion();
void certifcate_setOverrideVersion(int override);
bool certificate_CheckIfReceivedNew();
int certificate_get_stack_watermark();


#ifdef __cplusplus
}
#endif

#endif  /*_CERTIFICATE_H_*/
