#ifndef _OCMF_H_
#define _OCMF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"

#define LOG_STRING_SIZE 15000

void OCMF_Init();
int OCMF_CreateNewOCMFMessage(char * newMessage);
char * OCMF_CreateNewOCMFLog();
cJSON * OCMF_AddElementToOCMFLog(const char * const tx, const char * const st);
int OCMF_FinalizeOCMFLog();

#ifdef __cplusplus
}
#endif

#endif  /*_OCMF_H_*/
