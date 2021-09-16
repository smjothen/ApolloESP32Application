
#ifndef WIFI_CONFIG_SERVICE_H_
    #define WIFI_CONFIG_SERVICE_H_
    #include <stdint.h>

	#include "esp_gatt_defs.h"
    #include "esp_gatts_api.h"

	enum
	{
		// Service index
		WIFI_SERV_CHAR,
		WIFI_UUID,
		WIFI_DESCR,
		//WIFI_CFG,

        WIFI_SSID_CHAR,
        WIFI_SSID_UUID,
        WIFI_SSID_DESCR,
        //WIFI_SSID_CFG,

        WIFI_PSK_CHAR,
        WIFI_PSK_UUID,
        WIFI_PSK_DESCR,
        //WIFI_PSK_CFG,

		CHARGER_DEVICE_MID_CHAR,
		CHARGER_DEVICE_MID_UUID,
		CHARGER_DEVICE_MID_DESCR,
		//CHARGER_DEVICE_MID_CFG,

		//CHARGER_PIN_CHAR,
		//CHARGER_PIN_UUID,
		//CHARGER_PIN_DESCR,
		//CHARGER_PIN_CFG,

		CHARGER_AVAIL_WIFI_CHAR,
		CHARGER_AVAIL_WIFI_UUID,
		CHARGER_AVAIL_WIFI_DESCR,
		//CHARGER_AVAIL_WIFI_CFG,

		CHARGER_NETWORK_STATUS_CHAR,
		CHARGER_NETWORK_STATUS_UUID,
		CHARGER_NETWORK_STATUS_DESCR,
		//CHARGER_NETWORK_STATUS_CFG,

		CHARGER_AUTH_CHAR,
		CHARGER_AUTH_UUID,
		CHARGER_AUTH_DESCR,
		//CHARGER_AUTH_CFG,

		CHARGER_SAVE_CHAR,
		CHARGER_SAVE_UUID,
		CHARGER_SAVE_DESCR,
		//CHARGER_SAVE_CFG,

		CHARGER_HMI_BRIGHTNESS_CHAR,
		CHARGER_HMI_BRIGHTNESS_UUID,
		CHARGER_HMI_BRIGHTNESS_DESCR,
		//CHARGER_HMI_BRIGHTNESS_CFG,

		CHARGER_COMMUNICATION_MODE_CHAR,
		CHARGER_COMMUNICATION_MODE_UUID,
		CHARGER_COMMUNICATION_MODE_DESCR,
		//CHARGER_COMMUNICATION_MODE_CFG,

		CHARGER_FIRMWARE_VERSION_CHAR,
		CHARGER_FIRMWARE_VERSION_UUID,
		CHARGER_FIRMWARE_VERSION_DESCR,
		//CHARGER_FIRMWARE_VERSION_CFG,

		CHARGER_OPERATION_STATE_CHAR,
		CHARGER_OPERATION_STATE_UUID,
		CHARGER_OPERATION_STATE_DESCR,

		CHARGER_PAIR_NFC_TAG_CHAR,
		CHARGER_PAIR_NFC_TAG_UUID,

		CHARGER_AUTHORIZATION_RESULT_CHAR,
		CHARGER_AUTHORIZATION_RESULT_UUID,

		CHARGER_AUTH_UUID_CHAR,
		CHARGER_AUTH_UUID_UUID,

		CHARGER_OCCUPIED_STATE_CHAR,
		CHARGER_OCCUPIED_STATE_UUID,

		CHARGER_NETWORK_TYPE_CHAR,
		CHARGER_NETWORK_TYPE_UUID,
		CHARGER_NETWORK_TYPE_DESCR,

		CHARGER_STANDALONE_CHAR,
		CHARGER_STANDALONE_UUID,
		CHARGER_STANDALONE_DESCR,
		//CHARGER_STANDALONE_CFG,

		CHARGER_STANDALONE_PHASE_CHAR,
		CHARGER_STANDALONE_PHASE_UUID,
		CHARGER_STANDALONE_PHASE_DESCR,
		//CHARGER_STANDALONE_PHASE_CFG,

		CHARGER_STANDALONE_CURRENT_CHAR,
		CHARGER_STANDALONE_CURRENT_UUID,
		CHARGER_STANDALONE_CURRENT_DESCR,
		//CHARGER_STANDALONE_CURRENT_CFG,

		CHARGER_PERMANENT_LOCK_CHAR,
		CHARGER_PERMANENT_LOCK_UUID,
		CHARGER_PERMANENT_LOCK_DESCR,
		//CHARGER_PERMANENT_LOCK_CFG,

		CHARGER_WARNINGS_CHAR,
		CHARGER_WARNINGS_UUID,
		CHARGER_WARNINGS_DESCR,
		//CHARGER_WARNINGS_CFG,

		CHARGER_WIFI_MAC_CHAR,
		CHARGER_WIFI_MAC_UUID,
		CHARGER_WIFI_MAC_DESCR,
		//CHARGER_WIFI_MAC_CFG,

		CHARGER_MAX_INST_CURRENT_SWITCH_CHAR,
		CHARGER_MAX_INST_CURRENT_SWITCH_UUID,
		CHARGER_MAX_INST_CURRENT_SWITCH_DESCR,
		//CHARGER_MAX_INST_CURRENT_SWITCH_CFG,

		CHARGER_MAX_INST_CURRENT_CONFIG_CHAR,
		CHARGER_MAX_INST_CURRENT_CONFIG_UUID,
		CHARGER_MAX_INST_CURRENT_CONFIG_DESCR,
		//CHARGER_MAX_INST_CURRENT_CONFIG_CFG,

		CHARGER_PHASE_ROTATION_CHAR,
		CHARGER_PHASE_ROTATION_UUID,
		CHARGER_PHASE_ROTATION_DESCR,
		//CHARGER_PHASE_ROTATION_CFG,


		CHARGER_RUN_COMMAND_CHAR,
		CHARGER_RUN_COMMAND_UUID,

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
    //void SetNFCPairingStateOK();

#endif /* WIFI_CONFIG_SERVICE_H_ */
