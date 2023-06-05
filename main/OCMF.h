#ifndef _OCMF_H_
#define _OCMF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"

#define LOG_STRING_SIZE 20000

void OCMF_Init();

/******_SignedMeterValue_*****/
double OCMF_GetLastAccumulated_Energy();
int OCMF_SignedMeterValue_CreateNewOCMFMessage(char * newMessage, time_t *time_out, double *energy_out);
int  OCMF_SignedMeterValue_CreateMessageFromLog(char *new_message, time_t time_in, double energy_in);
//char * OCMF_CreateNewOCMFLog();

double get_accumulated_energy();

/******* CompletedSession *****/
bool OCMF_GetEnergyFault();
void OCMF_CompletedSession_StartStopOCMFLog(char label, time_t startTimeSec);
void OCMF_CompletedSession_AddElementToOCMFLog(char tx, time_t time_in, double energy_in);
//char * OCMF_CompletedSession_CreateNewOCMFLogFromFile();
esp_err_t OCMF_CompletedSession_CreateNewMessageFile(int oldestFile, char * messageString);
int OCMF_CompletedSession_FinalizeOCMFLog();

#ifdef __cplusplus
}
#endif

#endif  /*_OCMF_H_*/
