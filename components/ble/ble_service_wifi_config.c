
#include "ble_service_wifi_config.h"

//#include "/Users/eirik/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_gatts_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

#include "ble_common.h"
//#include "../ble_common.h"
#include "../../../esp-idf/components/json/cJSON/cJSON.h"
#include <string.h>

#include "../../main/storage.h"
#include "../../components/wifi/include/network.h"
#include "string.h"
#include "esp_wifi.h"

#include "../../main/DeviceInfo.h"
#include "../zaptec_protocol/include/protocol_task.h"

//#define TAG "ble wifi service"
static const char *TAG = "BLE SERVICE";
//========================================================================
//		Wifi Service
//========================================================================


//10492c5a-deec-4577-a25a-6950c0b5fd07


const uint16_t WIFI_SERV_uuid 				        = 0x00FF;//0x1801;//0x00FF;
const uint16_t WIFI_SERV_uuid2 				        = 0x00FE;
//const uint16_t WIFI_SERV_uuid 				        = 0;
//static const uint16_t WIFI_SERV_CHAR_info_uuid      = 0xFF01;
//static const uint16_t WIFI_SERV_CHAR_config_uuid    = 0xFF02;

////////////////////
//static const uint16_t CHARGER_SERV_CHAR_config_uuid    = 0xABCD;

static bool wasValid = false;
//static int nextIndex = 0;
static int nrOfWifiSegments = 0;
static int wifiRemainder = 0;

static int pinRetryCounter = 0;

uint8_t apNr = 0;
uint16_t maxAp = 10;
wifi_ap_record_t ap_records[10];

//10492c5a-deec-4577-a25a-6950c0b5fcd3
//const uint8_t	WifiSSID_uuid[ESP_UUID_LEN_128] = {0xd3, 0xfc, 0xb5 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};


///////////////////

const uint8_t Wifi_SERVICE_uuid[ESP_UUID_LEN_128] 		= {0x07, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WIFI_SERV_CHAR_descr[]  			= "ZAP Service";
static uint8_t WIFI_SERV_CHAR_val[32];
//static const uint8_t WIFI_SERV_descr[]              	= "ZAPTEC Service";


const uint8_t WifiSSID_uuid[ESP_UUID_LEN_128] 			= {0xd3, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t WIFI_SERV_CHAR_SSID_descr[]  		= "WiFi SSID";
static uint8_t WIFI_SERV_CHAR_SSID_val[32];        		//{0x00};

const uint8_t WiFiPSK_uid128[ESP_UUID_LEN_128] 			= {0xd4, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t WIFI_SERV_CHAR_PSK_descr[]   		= "Wifi password";
static uint8_t WIFI_SERV_CHAR_PSK_val[64]          		= {0x00};

const uint8_t DeviceMID_uuid128[ESP_UUID_LEN_128] 		= {0xd7, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t CHARGER_SERV_CHAR_CHARGER_MID_descr[]  = "Device MID";
static uint8_t CHARGER_SERV_CHAR_CHARGER_MID_val[9];

//const uint8_t 	PIN_uuid128[ESP_UUID_LEN_128] 			= {0xd8, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t CHARGER_SERV_CHAR_pin_descr[]  	= "Set PIN";
static uint8_t WIFI_SERV_CHAR_PIN_val[4]        		= {0x30, 0x30, 0x30, 0x30};

const uint8_t 	AvailableWifi_uuid128[ESP_UUID_LEN_128] = {0x01, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t AVAILABLE_WIFI_CHAR_pin_descr[]  	= "Available WiFi Networks";
//static uint8_t AVAILABLE_WIFI_SERV_CHAR_val[400];//      = {'a', 'c', 'e'};

const uint8_t 	NetworkStatus_uuid128[ESP_UUID_LEN_128] = {0x02, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t NETWORK_STATUS_CHAR_pin_descr[]  	= "Network Status";
//static uint8_t NETWORK_STATUS_SERV_CHAR_val[300];//      = {0x00, 0x00, 0x00, 0x00};

const uint8_t 	Auth_uuid128[ESP_UUID_LEN_128] 			= {0x00, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t AUTH_CHAR_pin_descr[]  			= "Auth";
static uint8_t AUTH_SERV_CHAR_val[]        				= {"0"};


const uint8_t 	Save_uuid128[ESP_UUID_LEN_128] 			= {0xd5, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t SAVE_CHAR_pin_descr[]  			= "Save";
static uint8_t SAVE_SERV_CHAR_val[]        				= {"0"};


const uint8_t HmiBrightness_uid128[ESP_UUID_LEN_128] 	= {0x09, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t HMI_BRIGHTNESS_descr[]  		 	= "HMI brightness";
static uint8_t HMI_BRIGHTNESS_val[8]          			= {0x00};

const uint8_t CommunicationMode_uid128[ESP_UUID_LEN_128] = {0xd2, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t COMMUNICATION_MODE_CHAR_descr[]   	= "Communication Mode";
static uint8_t COMMUNICATION_MODE_val[8]          		= {0x00};

const uint8_t FirmwareVersion_uid128[ESP_UUID_LEN_128] = {0x00, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t FIRMWARE_VERSION_CHAR_descr[]   	= "Firmware Version";
static uint8_t FIRMWARE_VERSION_val[8]          		= {0x00};



const uint8_t Standalone_uid128[ESP_UUID_LEN_128] 		= {0xd9, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Standalone_descr[]  			 	= "Standalone";
static uint8_t Standalone_val[8]          				= {0x00};

const uint8_t Standalone_Phase_uid128[ESP_UUID_LEN_128] = {0x06, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t StandalonePhase_descr[]   			= "Standalone Phase";
//static uint8_t Standalone Phase_val[8]          		= {0x00};

const uint8_t Standalone_Current_uid128[ESP_UUID_LEN_128] = {0x04, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Standalone_Current_descr[]   		= "Standalone Current";
static uint8_t Standalone_Current_val[8]          		= {0x00};

const uint8_t Permanent_Lock_uid128[ESP_UUID_LEN_128]	= {0x08, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Permanent_Lock_descr[]   			= "Permanent Lock";
static uint8_t Permanent_Lock_val[1]          			= {0x0};

const uint8_t 	Warnings_uuid128[ESP_UUID_LEN_128] 		= {0x01, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Warnings_descr[]  					= "Warnings";
static uint8_t Warnings_val[8]        					= {0x00};


const uint8_t Wifi_MAC_uid128[ESP_UUID_LEN_128] 		= {0x05, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Wifi_MAC_descr[]   				= "Wifi MAC";
static uint8_t Wifi_MAC_val[17]          				= {0x00};

const uint8_t Max_Inst_Current_Switch_uid128[ESP_UUID_LEN_128] 	= {0x06, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Max_Inst_Current_Switch_descr[]   	= "Max Installation Current Switch";
static uint8_t Max_Inst_Current_Switch_val[8]          	= {0x00};

const uint8_t Max_Inst_Current_Config_uid128[ESP_UUID_LEN_128] = {0x07, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Max_Inst_Current_Config_descr[]   	= "Max Installation Current Config";
static uint8_t Max_Inst_Current_Config_val[8]          	= {0x00};

const uint8_t Phase_Rotation_uid128[ESP_UUID_LEN_128] 	= {0x08, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t Phase_Rotation_descr[]   			= "Phase Rotation";
static uint8_t Phase_Rotation_val[1]          			= {0x0};


//static uint8_t WIFI_SERV_CHAR_info_ccc[2]           	= {0x00,0x00};
//static uint8_t WIFI_SERV_CHAR_config_ccc[2]         	= {0x00,0x00};
static uint8_t CHARGER_SERV_CHAR_config_ccc[2]      	= {0x11,0x22};

static char wifiPackage[500] = {0};







const esp_gatts_attr_db_t wifi_serv_gatt_db[WIFI_NB] =
{
	[WIFI_SERV_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_SERVICE_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uid128, ESP_GATT_PERM_READ, 0, 0, (uint8_t *)&WIFI_SERV_uuid}},

    //[WIFI_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_SERVICE_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[WIFI_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &WIFI_SERV_CHAR_descr, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	//[WIFI_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ,  sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},
	//[WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_info_ccc), (uint8_t *)WIFI_SERV_CHAR_info_ccc}},

	[WIFI_SSID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[WIFI_SSID_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &WifiSSID_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[WIFI_SSID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_info_ccc), (uint8_t *)WIFI_SERV_CHAR_info_ccc}},

	[WIFI_PSK_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
    [WIFI_PSK_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &WiFiPSK_uid128, ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
    [WIFI_PSK_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
    //[WIFI_PSK_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_config_ccc), (uint8_t *)WIFI_SERV_CHAR_config_ccc}},

	[CHARGER_DEVICE_MID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_DEVICE_MID_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[CHARGER_DEVICE_MID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_DEVICE_MID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

//	[CHARGER_PIN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
//	[CHARGER_PIN_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &PIN_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
//	[CHARGER_PIN_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_PIN_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},


	[CHARGER_AVAIL_WIFI_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AVAIL_WIFI_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &AvailableWifi_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_AVAIL_WIFI_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_AVAIL_WIFI_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_NETWORK_STATUS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_NETWORK_STATUS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	//[CHARGER_NETWORK_STATUS_UUID] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	[CHARGER_NETWORK_STATUS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_NETWORK_STATUS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_WARNINGS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_WARNINGS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Warnings_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_WARNINGS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_WARNINGS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_AUTH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AUTH_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Auth_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[CHARGER_AUTH_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_AUTH_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_SAVE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_SAVE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Save_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[CHARGER_SAVE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_SAVE_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_HMI_BRIGHTNESS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_HMI_BRIGHTNESS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &HmiBrightness_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[CHARGER_HMI_BRIGHTNESS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_COMMUNICATION_MODE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_COMMUNICATION_MODE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &CommunicationMode_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[CHARGER_COMMUNICATION_MODE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_FIRMWARE_VERSION_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_FIRMWARE_VERSION_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &FirmwareVersion_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_FIRMWARE_VERSION_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},




	[CHARGER_STAND_ALONE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STAND_ALONE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_STAND_ALONE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_STAND_ALONE_PHASE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STAND_ALONE_PHASE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_Phase_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_STAND_ALONE_PHASE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_STAND_ALONE_CURRENT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STAND_ALONE_CURRENT_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_Current_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_STAND_ALONE_CURRENT_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_PERMANENT_LOCK_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_PERMANENT_LOCK_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Permanent_Lock_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_PERMANENT_LOCK_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_WARNINGS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_WARNINGS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Warnings_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_WARNINGS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_WIFI_MAC_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_WIFI_MAC_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_MAC_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_WIFI_MAC_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_MAX_INST_CURRENT_SWITCH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_MAX_INST_CURRENT_SWITCH_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Max_Inst_Current_Switch_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_MAX_INST_CURRENT_SWITCH_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_MAX_INST_CURRENT_CONFIG_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_MAX_INST_CURRENT_CONFIG_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Max_Inst_Current_Config_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_MAX_INST_CURRENT_CONFIG_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_PHASE_ROTATION_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_PHASE_ROTATION_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Phase_Rotation_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[CHARGER_PHASE_ROTATION_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

};

void charInit()
{
#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
}


void setDeviceNameAsChar(char * devName)
{
	memcpy(CHARGER_SERV_CHAR_CHARGER_MID_val, devName, 9);
}

void setPinAsChar(char * pin)
{
	memcpy(WIFI_SERV_CHAR_PIN_val, pin, 4);
}


uint16_t getAttributeIndexByWifiHandle(uint16_t attributeHandle)
{
	// Get the attribute index in the attribute table by the returned handle

    uint16_t attrIndex = WIFI_NB;
    uint16_t index;

    for(index = 0; index < WIFI_NB; ++index)
    {
        if( wifi_handle_table[index] == attributeHandle )
        {
            attrIndex = index;
            break;
        }
    }

    return attrIndex;
}


static float hmiBrightness = 0.0;
static char nrTostr[8];
//static char swVersion[8];

void handleWifiReadEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{
	//Check authentication before allowing reads
//	if((AUTH_SERV_CHAR_val[0] == 0) && (attrIndex != CHARGER_DEVICE_MID_UUID))
//	{
//		ESP_LOGE(TAG, "Read: No pin set: %d", attrIndex);
//		return;
//	}

	char *jsonString;
	//char jsonString[] = "{\"wifi\":{\"ip\":\"10.0.0.1\",\"link\":-54},\"online\":true}";//"{'{', '"', 'o', 'n', 'l', 'i', 'n', 'e', '"', '=', 't', 'r', 'u', 'e', '}', '\0'};
	//char jsonString[] = "{\"online\":true}";
	//int jsonStringLen = strlen(jsonString);

	static int statusSegmentCount = 0;
	if(attrIndex != CHARGER_NETWORK_STATUS_UUID)
		statusSegmentCount = 0;

	static int wifiSegmentCount = 0;
	if(attrIndex != CHARGER_AVAIL_WIFI_UUID)
		wifiSegmentCount = 0;

	cJSON *jsonObject;
	cJSON *wifiObject;
	ESP_LOGE(TAG, "BLE index: %d", attrIndex);
    switch( attrIndex )
    {
    // Characteristic read values
    /*
     * Write only
     case WIFI_PSK_UUID:
	    memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
	    memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_info_val, sizeof(WIFI_SERV_CHAR_info_val));
	    rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_info_val);
	    break;*/

    case WIFI_SSID_UUID:
	    memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

	    char * SSID = network_getWifiSSID();
	    int len = strlen(SSID);
	    //memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_SSID_val, sizeof(WIFI_SERV_CHAR_SSID_val));
	    //memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_SSID_val, len);
	    memcpy(rsp->attr_value.value, SSID, len);
	    rsp->attr_value.len = len;//sizeof(WIFI_SERV_CHAR_SSID_val);
	    break;


    // Characteristic descriptions
    case WIFI_PSK_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
        memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_PSK_descr, sizeof(WIFI_SERV_CHAR_PSK_descr));
        rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_PSK_descr);
        break;

    case WIFI_SSID_DESCR:
	    memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
        memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_SSID_descr, sizeof(WIFI_SERV_CHAR_SSID_descr));
        rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_SSID_descr);
        break;


    case CHARGER_DEVICE_MID_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, CHARGER_SERV_CHAR_CHARGER_MID_val, sizeof(CHARGER_SERV_CHAR_CHARGER_MID_val));
		rsp->attr_value.len = sizeof(CHARGER_SERV_CHAR_CHARGER_MID_val);
		break;

    case CHARGER_DEVICE_MID_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, CHARGER_SERV_CHAR_CHARGER_MID_descr, sizeof(CHARGER_SERV_CHAR_CHARGER_MID_descr));
		rsp->attr_value.len = sizeof(CHARGER_SERV_CHAR_CHARGER_MID_descr);
		break;

//    case CHARGER_PIN_UUID:
//		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
//		memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_PIN_val, sizeof(WIFI_SERV_CHAR_PIN_val));
//		rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_PIN_val);
//		break;
//
//    case CHARGER_PIN_DESCR:
//		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
//		memcpy(rsp->attr_value.value, CHARGER_SERV_CHAR_pin_descr, sizeof(CHARGER_SERV_CHAR_pin_descr));
//		rsp->attr_value.len = sizeof(CHARGER_SERV_CHAR_pin_descr);
//		break;

    case CHARGER_AUTH_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, AUTH_CHAR_pin_descr, sizeof(AUTH_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(AUTH_CHAR_pin_descr);
		break;
    case CHARGER_SAVE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, SAVE_CHAR_pin_descr, sizeof(SAVE_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(SAVE_CHAR_pin_descr);
		break;

    case CHARGER_AVAIL_WIFI_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, AVAILABLE_WIFI_CHAR_pin_descr, sizeof(AVAILABLE_WIFI_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(AVAILABLE_WIFI_CHAR_pin_descr);
		break;

    case CHARGER_AVAIL_WIFI_UUID:

    	if(wifiSegmentCount == 0)
    	{
    		apNr = 0;
			nrOfWifiSegments = 0;
			wifiRemainder = 0;
    		memset(wifiPackage, 0, 500);

    		network_startWifiScan();

    		esp_wifi_scan_get_ap_num(&apNr);
			ESP_LOGE(TAG, "No of APs: %d", apNr);

			if(apNr > 0)
			{
				if(apNr > maxAp)
					apNr = maxAp;
				esp_wifi_scan_get_ap_records(&maxAp, ap_records);
				int i;
				for (i = 0; i < apNr; i++)
					ESP_LOGE(TAG, "SSID %d: %s, sig: %d", i+1, ap_records[i].ssid, ap_records[i].rssi);
			}

    	}

		if(apNr > 0)
		{
			//(sizeof(ap_records) * 10) + (5*10)
			//char package[500] = {0};
			wifiPackage[0] = 0;	//version
			wifiPackage[1] = apNr; //Nr of discovered access points

			int nextIndex = 2;
			int i;
			for (i = 0; i < apNr; i++)
			{
				wifiPackage[nextIndex++] = ap_records[apNr].rssi;

				if(ap_records[i].authmode == WIFI_AUTH_OPEN)
					wifiPackage[nextIndex++] = 0;
				else
					wifiPackage[nextIndex++] = 1;

				//Add SSID length to package
				int ssidLen = strlen((char*)ap_records[i].ssid);
				if (ssidLen <= 32)
					wifiPackage[nextIndex++] = ssidLen;

				//Add SSID name to package
				memcpy(&wifiPackage[nextIndex], &ap_records[i].ssid, ssidLen);
				nextIndex += ssidLen;

				///if(network_wifiIsValid() == false)
					///network_stopWifi();
			}

			int MTUsize = 22;
			nrOfWifiSegments = nextIndex/MTUsize;
			wifiRemainder = nextIndex % MTUsize;


			if (wifiSegmentCount <= nrOfWifiSegments - 1)
			{
				memcpy(rsp->attr_value.value, &wifiPackage[MTUsize*wifiSegmentCount], MTUsize*2);
				rsp->attr_value.offset = MTUsize*(wifiSegmentCount);
				rsp->attr_value.len = MTUsize;
				wifiSegmentCount++;
			}
			else if (wifiSegmentCount == nrOfWifiSegments)
			{
				memcpy(rsp->attr_value.value, &wifiPackage[MTUsize*wifiSegmentCount], wifiRemainder);
				rsp->attr_value.offset = MTUsize*(wifiSegmentCount);
				rsp->attr_value.len = wifiRemainder;
				apNr = 0;
				wifiSegmentCount = 0;
			}

		}
    	break;

    case CHARGER_NETWORK_STATUS_UUID:

		//{"wifi":{"ip":"10.0.0.1","link":-54},"online":true}

    	ESP_LOGI(TAG, "BLE IP4 Address: %s", network_GetIP4Address());

		jsonObject = cJSON_CreateObject();


		cJSON_AddItemToObject(jsonObject, "wifi", wifiObject=cJSON_CreateObject());
		cJSON_AddStringToObject(wifiObject, "ip", network_GetIP4Address());
		cJSON_AddNumberToObject(wifiObject, "link", (int)network_WifiSignalStrength());

		cJSON_AddBoolToObject(jsonObject, "online", network_WifiIsConnected());

		//jsonString = cJSON_Print(jsonObject);
		jsonString = cJSON_PrintUnformatted(jsonObject);

		int jsonStringLen = strlen(jsonString);

		int nrOfSegments = jsonStringLen/22;
		int segmentRemainder = jsonStringLen % 22;
		if (statusSegmentCount <= nrOfSegments - 1)
		{
			memcpy(rsp->attr_value.value, &jsonString[22*statusSegmentCount], 22);
			rsp->attr_value.offset = 22*statusSegmentCount;
			rsp->attr_value.len = 22;
		}
		else if (statusSegmentCount == nrOfSegments)
		{
			memcpy(rsp->attr_value.value, &jsonString[22*statusSegmentCount], segmentRemainder);
			rsp->attr_value.offset = 22*statusSegmentCount;
			rsp->attr_value.len = segmentRemainder;
		}
		statusSegmentCount++;

		//Clean up heap items
		free(jsonString);
		//cJSON_Delete(wifiObject);
		cJSON_Delete(jsonObject);

    	break;

    case CHARGER_AUTH_UUID:
    	rsp->attr_value.value[0] = AUTH_SERV_CHAR_val[0];
    	rsp->attr_value.len = 1;
    	break;

    case CHARGER_SAVE_UUID:
    	rsp->attr_value.value[0] = SAVE_SERV_CHAR_val[0];
    	rsp->attr_value.len = 1;
        break;

    case CHARGER_HMI_BRIGHTNESS_UUID:
    	hmiBrightness = storage_Get_HmiBrightness();
    	memset(nrTostr, 0, sizeof(nrTostr));
    	sprintf(nrTostr, "%1.3f", hmiBrightness);
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);
		break;

    case CHARGER_HMI_BRIGHTNESS_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, HMI_BRIGHTNESS_descr, sizeof(HMI_BRIGHTNESS_descr));
		rsp->attr_value.len = sizeof(HMI_BRIGHTNESS_descr);
		break;

    case CHARGER_COMMUNICATION_MODE_UUID:

		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
		{
			memcpy(COMMUNICATION_MODE_val, "Wifi",4);
			memcpy(rsp->attr_value.value, COMMUNICATION_MODE_val, sizeof(COMMUNICATION_MODE_val));
			rsp->attr_value.len = 4;
			ESP_LOGI(TAG, "Read Wifi");
		}
		else if (storage_Get_CommunicationMode() == eCONNECTION_4G)
		{
			memcpy(COMMUNICATION_MODE_val, "4G",2);
			memcpy(rsp->attr_value.value, COMMUNICATION_MODE_val, sizeof(COMMUNICATION_MODE_val));
			rsp->attr_value.len = 2;
			ESP_LOGI(TAG, "Read 4G");
		}
		else
		{
			memcpy(COMMUNICATION_MODE_val, "None",4);
			memcpy(rsp->attr_value.value, COMMUNICATION_MODE_val, sizeof(COMMUNICATION_MODE_val));
			rsp->attr_value.len = 4;
			ESP_LOGI(TAG, "Read None");
		}
		break;

    case CHARGER_COMMUNICATION_MODE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, COMMUNICATION_MODE_CHAR_descr, sizeof(COMMUNICATION_MODE_CHAR_descr));
		rsp->attr_value.len = sizeof(COMMUNICATION_MODE_CHAR_descr);
		break;

    case CHARGER_FIRMWARE_VERSION_UUID:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		memcpy(rsp->attr_value.value, softwareVersion, strlen(softwareVersion));
 		rsp->attr_value.len = strlen(softwareVersion);
 		break;

    case CHARGER_FIRMWARE_VERSION_DESCR:
     	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		memcpy(rsp->attr_value.value, FIRMWARE_VERSION_CHAR_descr, sizeof(FIRMWARE_VERSION_CHAR_descr));
 		rsp->attr_value.len = sizeof(FIRMWARE_VERSION_CHAR_descr);
 		break;




    case CHARGER_STAND_ALONE_UUID:

    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	ESP_LOGI(TAG, "Read Standalone %d ", storage_Get_Standalone());

    	if(storage_Get_Standalone() == 0)
    	{
			memcpy(rsp->attr_value.value, "0", 1);
			rsp->attr_value.len = 1;
    	}
    	else if(storage_Get_Standalone() == 1)
    	{
			memcpy(rsp->attr_value.value, "1", 1);
			rsp->attr_value.len = 1;
    	}

		break;

    case CHARGER_STAND_ALONE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Standalone_descr, sizeof(Standalone_descr));
		rsp->attr_value.len = sizeof(Standalone_descr);
		break;


    case CHARGER_STAND_ALONE_PHASE_UUID:

 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	ESP_LOGI(TAG, "Read Standalone Phase %d ", storage_Get_StandalonePhase());

    	memset(nrTostr, 0, sizeof(nrTostr));
    	itoa(storage_Get_StandalonePhase(), nrTostr, 10);

    	memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

 		break;

     case CHARGER_STAND_ALONE_PHASE_DESCR:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		memcpy(rsp->attr_value.value, StandalonePhase_descr, sizeof(StandalonePhase_descr));
 		rsp->attr_value.len = sizeof(StandalonePhase_descr);
 		break;


     case CHARGER_STAND_ALONE_CURRENT_UUID:

		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		ESP_LOGI(TAG, "Read Standalone Current %f ", storage_Get_StandaloneCurrent());

		memset(nrTostr, 0, sizeof(nrTostr));
		sprintf(nrTostr, "%.1f", storage_Get_StandaloneCurrent());

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

     case CHARGER_STAND_ALONE_CURRENT_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Standalone_Current_descr, sizeof(Standalone_Current_descr));
  		rsp->attr_value.len = sizeof(Standalone_Current_descr);
  		break;

     case CHARGER_PERMANENT_LOCK_UUID:

 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

 		ESP_LOGI(TAG, "Read Permanent Lock %d ", storage_Get_PermanentLock());
 		memset(nrTostr, 0, sizeof(nrTostr));
 		itoa(storage_Get_PermanentLock(), nrTostr, 10);

 		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
 		rsp->attr_value.len = strlen(nrTostr);

 		break;

     case CHARGER_PERMANENT_LOCK_DESCR:
   		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
   		memcpy(rsp->attr_value.value, Permanent_Lock_descr, sizeof(Permanent_Lock_descr));
   		rsp->attr_value.len = sizeof(Permanent_Lock_descr);
   		break;

     case CHARGER_WARNINGS_UUID:

  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

  		ESP_LOGI(TAG, "Read Warning %d ", MCU_GetWarnings());

  		memset(nrTostr, 0, sizeof(nrTostr));
  		itoa(MCU_GetWarnings(), nrTostr, 10);

  		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
  		rsp->attr_value.len = strlen(nrTostr);

  		break;

	case CHARGER_WARNINGS_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Warnings_descr, sizeof(Warnings_descr));
		rsp->attr_value.len = sizeof(Warnings_descr);
		break;



    case CHARGER_WIFI_MAC_UUID:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

 		volatile uint8_t wifiMAC[18] = {0};
 		esp_err_t err = esp_read_mac(wifiMAC, 0); //0=Wifi station
 		sprintf(wifiMAC, "%02x:%02x:%02x:%02x:%02x:%02x", wifiMAC[0],wifiMAC[1],wifiMAC[2],wifiMAC[3],wifiMAC[4],wifiMAC[5]);

 		ESP_LOGI(TAG, "Read Wifi MAC: %s, len %d", (char*)wifiMAC, strlen((char*)wifiMAC));

 		memcpy(rsp->attr_value.value, wifiMAC, 17);
 		rsp->attr_value.len = 17;

 		break;

	case CHARGER_WIFI_MAC_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Wifi_MAC_descr, sizeof(Wifi_MAC_descr));
		rsp->attr_value.len = sizeof(Wifi_MAC_descr);
		break;

    case CHARGER_MAX_INST_CURRENT_SWITCH_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		ESP_LOGI(TAG, "Read Max installation current SWITCH %f A", MCU_GetMaxInstallationCurrentSwitch());

		memset(nrTostr, 0, sizeof(nrTostr));
		sprintf(nrTostr, "%.1f", MCU_GetMaxInstallationCurrentSwitch());

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

    case CHARGER_MAX_INST_CURRENT_SWITCH_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Max_Inst_Current_Switch_descr, sizeof(Max_Inst_Current_Switch_descr));
  		rsp->attr_value.len = sizeof(Max_Inst_Current_Switch_descr);
  		break;


    case CHARGER_MAX_INST_CURRENT_CONFIG_UUID:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		ESP_LOGI(TAG, "Read Max installation current CONFIG %f A", storage_Get_MaxInstallationCurrentConfig());

		memset(nrTostr, 0, sizeof(nrTostr));
		sprintf(nrTostr, "%.1f", storage_Get_MaxInstallationCurrentConfig());

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

    case CHARGER_MAX_INST_CURRENT_CONFIG_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Max_Inst_Current_Config_descr, sizeof(Max_Inst_Current_Config_descr));
  		rsp->attr_value.len = sizeof(Max_Inst_Current_Config_descr);
  		break;

    case CHARGER_PHASE_ROTATION_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		ESP_LOGI(TAG, "Read PhaseRotation %d ", storage_Get_PhaseRotation());
		memset(nrTostr, 0, sizeof(nrTostr));
		itoa(storage_Get_PhaseRotation(), nrTostr, 10);

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

    case CHARGER_PHASE_ROTATION_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Phase_Rotation_descr, sizeof(Phase_Rotation_descr));
  		rsp->attr_value.len = sizeof(Phase_Rotation_descr);
  		break;


    }
}

static bool saveWifi = false;
static bool saveConfiguration = false;

void handleWifiWriteEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{
	//Check authentication before allowing writes
//	if((AUTH_SERV_CHAR_val[0] == 0) && (attrIndex != CHARGER_AUTH_UUID))
//	{
//		ESP_LOGE(TAG, "Write: No pin set: %d", attrIndex);
//		return;
//	}

    switch( attrIndex )
    {
    case WIFI_PSK_UUID:
        /*
         *  Handle any writes to Wifi Info char here
         */

        // This prints the first byte written to the characteristic
        ESP_LOGI(TAG, "Wifi Info characteristic written with %02x", param->write.value[0]);

        memset(WIFI_SERV_CHAR_PSK_val,0, sizeof(WIFI_SERV_CHAR_PSK_val));
		memcpy(WIFI_SERV_CHAR_PSK_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Wifi SSID %s", WIFI_SERV_CHAR_PSK_val);

		saveWifi = true;

//		storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);
//
//		//Make the values
//		network_CheckWifiParameters();
	    break;

    case WIFI_SSID_UUID:
        /*
         *  Handle any writes to Wifi Val char here
         */
    	ESP_LOGI(TAG, "Wifi Val characteristic written with %02x", param->write.value[0]);

    	memset(WIFI_SERV_CHAR_SSID_val,0, sizeof(WIFI_SERV_CHAR_SSID_val));
		memcpy(WIFI_SERV_CHAR_SSID_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Wifi SSID %s", WIFI_SERV_CHAR_SSID_val);

		saveWifi = true;
	    break;


    case CHARGER_HMI_BRIGHTNESS_UUID:

    	ESP_LOGI(TAG, "HMI brightness characteristic written with %02x", param->write.value[0]);

    	memset(HMI_BRIGHTNESS_val,0, sizeof(HMI_BRIGHTNESS_val));
		memcpy(HMI_BRIGHTNESS_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New hmiBrightness %s", HMI_BRIGHTNESS_val);

		float hmiBrightness = atof((char*)HMI_BRIGHTNESS_val);

		if((1.0 >= hmiBrightness) && (hmiBrightness >= 0.0 ))
		{
			storage_Set_HmiBrightness(hmiBrightness);
			ESP_LOGI(TAG, "Set hmiBrightness: %f", hmiBrightness);
			saveConfiguration = true;
		}

   		break;



    case CHARGER_COMMUNICATION_MODE_UUID:

    	ESP_LOGI(TAG, "Wifi Val characteristic written with %02x", param->write.value[0]);

    	memset(COMMUNICATION_MODE_val,0, sizeof(COMMUNICATION_MODE_val));
		memcpy(COMMUNICATION_MODE_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Communication Mode %s", COMMUNICATION_MODE_val);

		if(strncmp("Wifi", (char*)COMMUNICATION_MODE_val, 4) == 0)
		{
			storage_Set_CommunicationMode(eCONNECTION_WIFI);
			ESP_LOGI(TAG, "Set Wifi");

		}
		else if(strncmp("4G", (char*)COMMUNICATION_MODE_val, 2) == 0)
		{
			storage_Set_CommunicationMode(eCONNECTION_4G);
			ESP_LOGI(TAG, "Set 4G");
		}
		else
		{
			storage_Set_CommunicationMode(eCONNECTION_NONE);
			ESP_LOGI(TAG, "Set None");
		}

		saveConfiguration = true;

   		break;



    case CHARGER_AUTH_UUID:

		ESP_LOGI(TAG, "Adapter pin %02x", param->write.value[0]);

		if((param->write.value[0] == WIFI_SERV_CHAR_PIN_val[0]) &&
				(param->write.value[1] == WIFI_SERV_CHAR_PIN_val[1]) &&
				(param->write.value[2] == WIFI_SERV_CHAR_PIN_val[2]) &&
				(param->write.value[3] == WIFI_SERV_CHAR_PIN_val[3]))
		{

			memset(WIFI_SERV_CHAR_PIN_val,0, sizeof(WIFI_SERV_CHAR_PIN_val));
			memcpy(WIFI_SERV_CHAR_PIN_val,param->write.value, param->write.len);
			ESP_LOGI(TAG, "Auth value %s", AUTH_SERV_CHAR_val);

			pinRetryCounter = 0;
			AUTH_SERV_CHAR_val[0] = '1';
		}
		else
		{
			pinRetryCounter++;
			AUTH_SERV_CHAR_val[0] = '0';

			ESP_LOGE(TAG, "Wrong pin: %d", pinRetryCounter);

			if(pinRetryCounter == 5)
			{
				pinRetryCounter = 0;
				//If to many retries, sleep to drop app-connection
				vTaskDelay(pdMS_TO_TICKS(10000));
			}
		}

		break;

    case CHARGER_SAVE_UUID:
		ESP_LOGI(TAG, "Save value %02x", param->write.value[0]);

    	///wasValid = network_wifiIsValid();

		//SAVE_SERV_CHAR_val[0] = '1';
		if((param->write.value[0] == '1') && (saveWifi == true))
		{
			storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);

			//Make the values active
			network_CheckWifiParameters();

			saveWifi = false;
		}


		if((param->write.value[0] == '1') && (saveConfiguration == true))
		{
			storage_SaveConfiguration();

			saveConfiguration = false;
		}

		param->write.value[0] = 0;

		///if(wasValid == true)
			///network_updateWifi();

		ESP_LOGI(TAG, "Save val %s", SAVE_SERV_CHAR_val);

		break;
	}

}


void ClearAuthValue()
{
	AUTH_SERV_CHAR_val[0] = 0;
	SAVE_SERV_CHAR_val[0] = 0;
}
