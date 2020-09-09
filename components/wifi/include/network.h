#ifndef _CONNECT_H_
#define _CONNECT_H_

#ifdef __cplusplus
extern "C" {
#endif

void configure_wifi(int switchstate);
void SetupWifi();
bool network_WifiIsConnected();

void network_stopWifi();
void network_clearWifi();
char * network_GetIP4Address();
char * network_getWifiSSID();
float network_WifiSignalStrength();
bool network_wifiIsValid();

void network_startWifiScan();
bool network_renewConnection();
void network_updateWifi();

#ifdef __cplusplus
}
#endif

#endif  /*_CONNECT_H_*/
