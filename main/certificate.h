#ifndef _CERTIFICATE_H_
#define _CERTIFICATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool certificateValidate();
void certificate_init();
void certificate_update();


#ifdef __cplusplus
}
#endif

#endif  /*_CERTIFICATE_H_*/
