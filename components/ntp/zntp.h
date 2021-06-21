#ifndef _ZNTP_H_
#define _ZNTP_H_

#ifdef __cplusplus
extern "C" {
#endif


void zntp_init();
void zntp_checkSyncStatus();
bool zntp_IsSynced();
struct tm zntp_GetLatestNTPTime();
void zntp_GetSystemTime(char * buffer, time_t *now_out);
void zntp_format_time(char *buffer, time_t time_in);
bool zntp_Get15MinutePoint();
void zntp_restart();
void zntp_stop();
uint8_t zntp_enabled();


#ifdef __cplusplus
}
#endif

#endif  /*_ZNTP_H_*/
