#ifndef _ZNTP_H_
#define _ZNTP_H_

#ifdef __cplusplus
extern "C" {
#endif


void zntp_init();
void zntp_checkSyncStatus();

void zntp_restart();
void zntp_stop();
uint8_t zntp_enabled();

#ifdef __cplusplus
}
#endif

#endif  /*_ZNTP_H_*/
