#ifndef _CONNECT_H_
#define _CONNECT_H_

#ifdef __cplusplus
extern "C" {
#endif


enum sConfig
{
	eConfig_NVS			  	= 0,
	eConfig_Wifi_NVS	  	= 1,
	eConfig_Wifi_Zaptec 	= 2,
	eConfig_Wifi_Home_Wr32	= 3,
	eConfig_Wifi_EMC 		= 4,
	eConfig_Wifi_EMC_TCP    = 5,
	eConfig_Wifi_Post		= 6,
	eConfig_4G 				= 7,
	eConfig_4G_Post			= 8,
	eConfig_4G_bridge 		= 9
};

esp_err_t network_connect_wifi(void);
esp_err_t network_disconnect_wifi(void);

void configure_wifi();
void SetupWifi();
bool network_WifiIsConnected();
bool network_CheckWifiParameters();

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
