#ifndef _CONNECT_H_
#define _CONNECT_H_

#ifdef __cplusplus
extern "C" {
#endif

void configure_wifi(int switchstate);
void SetupWifi();
bool WifiIsConnected();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECT_H_*/
