
#ifndef WIFI_CONFIG_SERVICE_H_
    #define WIFI_CONFIG_SERVICE_H_
    #include <stdint.h>

    //#include "/Users/eirik/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_gatt_defs.h"
    //#include "/Users/eirik/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_gatts_api.h"
	#include "esp_gatt_defs.h"
    #include "esp_gatts_api.h"

	enum
	{
		// Service index
		WIFI_SERV_CHAR,
		WIFI_VAL,
		WIFI_DESCR,
		WIFI_CFG,

        WIFI_SSID_CHAR,
        WIFI_SSID_VAL,
        WIFI_SSID_DESCR,
        WIFI_SSID_CFG,

        WIFI_PSK_CHAR,
        WIFI_PSK_VAL,
        WIFI_PSK_DESCR,
        WIFI_PSK_CFG,

		ADAPTER_DEVICE_MID_CHAR,
		ADAPTER_DEVICE_MID_VAL,
		ADAPTER_DEVICE_MID_DESCR,
		ADAPTER_DEVICE_MID_CFG,

		ADAPTER_PIN_CHAR,
		ADAPTER_PIN_VAL,
		ADAPTER_PIN_DESCR,
		ADAPTER_PIN_CFG,

		ADAPTER_AVAIL_WIFI_CHAR,
		ADAPTER_AVAIL_WIFI_VAL,
		ADAPTER_AVAIL_WIFI_DESCR,
		ADAPTER_AVAIL_WIFI_CFG,

		ADAPTER_NETWORK_STATUS_CHAR,
		ADAPTER_NETWORK_STATUS_VAL,
		ADAPTER_NETWORK_STATUS_DESCR,
		ADAPTER_NETWORK_STATUS_CFG,

		ADAPTER_WARNINGS_CHAR,
		ADAPTER_WARNINGS_VAL,
		ADAPTER_WARNINGS_DESCR,
		ADAPTER_WARNINGS_CFG,

		ADAPTER_AUTH_CHAR,
		ADAPTER_AUTH_VAL,
		ADAPTER_AUTH_DESCR,
		ADAPTER_AUTH_CFG,

		ADAPTER_SAVE_CHAR,
		ADAPTER_SAVE_VAL,
		ADAPTER_SAVE_DESCR,
		ADAPTER_SAVE_CFG,

		WIFI_NB,
	};

    uint16_t wifi_handle_table[WIFI_NB];

    extern const uint16_t WIFI_SERV_uuid;
    extern const uint8_t Wifi_SERVICE_uuid[ESP_UUID_LEN_128];
    extern const esp_gatts_attr_db_t wifi_serv_gatt_db[WIFI_NB];

    extern const uint16_t WIFI_SERV_uuid2;
	extern const uint8_t Wifi_SERVICE_uuid2[ESP_UUID_LEN_128];
	extern const esp_gatts_attr_db_t wifi_serv_gatt_db2[WIFI_NB];

    uint16_t getAttributeIndexByWifiHandle(uint16_t attributeHandle);
    void handleWifiReadEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp);
    void handleWifiWriteEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp);
    void setDeviceNameAsChar(char * devName);
    void setPinAsChar(char * pin);
    void charInit();

    void ClearAuthValue();

#endif /* WIFI_CONFIG_SERVICE_H_ */
