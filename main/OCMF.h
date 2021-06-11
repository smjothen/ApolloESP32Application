#ifndef _OCMF_H_
#define _OCMF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"

#define LOG_STRING_SIZE 20000

void OCMF_Init();
int OCMF_CreateNewOCMFMessage(char * newMessage, time_t *time_out, double *energy_out);
char * OCMF_CreateNewOCMFLog();
cJSON * OCMF_AddElementToOCMFLog(const char * const tx, const char * const st);
int OCMF_FinalizeOCMFLog();

#ifdef __cplusplus
}
#endif

#endif  /*_OCMF_H_*/
