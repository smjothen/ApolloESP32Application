#ifndef _CERTIFICATE_H_
#define _CERTIFICATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool certificateValidate();
void certificate_init();
bool certificateOk();
void certificate_clear();
void certificate_update(int tls_error);
int certificate_GetCurrentBundleVersion();


#ifdef __cplusplus
}
#endif

#endif  /*_CERTIFICATE_H_*/
