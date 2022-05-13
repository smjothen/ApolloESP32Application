
#include "ble_service_wifi_config.h"


#include "esp_gatts_api.h"
#include "esp_log.h"

#include "ble_common.h"

#include "cJSON.h"
#include <string.h>

#include "../../main/storage.h"
#include "../../components/wifi/include/network.h"
#include "string.h"
#include "esp_wifi.h"

#include "../../main/DeviceInfo.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "../../main/connectivity.h"
#include "../cellular_modem/include/ppp_task.h"
#include "../apollo_ota/include/apollo_ota.h"
#include "../zaptec_cloud/include/zaptec_cloud_observations.h"
#include "../zaptec_cloud/include/zaptec_cloud_listener.h"
#include "../../main/sessionHandler.h"
#include "../../main/chargeSession.h"
#include "../../components/authentication/rfidPairing.h"
#include "../../components/authentication/authentication.h"


static const char *TAG = "BLE SERVICE";

const uint16_t WIFI_SERV_uuid 				        = 0x00FF;

static bool wasValid = false;
static int nrOfWifiSegments = 0;
static int wifiRemainder = 0;
static int MTUsize = 22;
static int pinRetryCounter = 0;

uint16_t apNr = 0;
uint16_t maxAp = 15;
wifi_ap_record_t ap_records[15];


#define USE_PIN
///////////////////

const uint8_t Wifi_SERVICE_uuid[ESP_UUID_LEN_128] 		= {0x07, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WIFI_SERV_CHAR_descr[]  			= "ZAP Service";

const uint8_t WifiSSID_uuid[ESP_UUID_LEN_128] 			= {0xd3, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WIFI_SERV_CHAR_SSID_descr[]  		= "WiFi SSID";
static uint8_t WIFI_SERV_CHAR_SSID_val[32];        		//{0x00};

const uint8_t WiFiPSK_uid128[ESP_UUID_LEN_128] 			= {0xd4, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WIFI_SERV_CHAR_PSK_descr[]   		= "Wifi password";
static uint8_t WIFI_SERV_CHAR_PSK_val[64]          		= {0x00};

const uint8_t DeviceMID_uuid128[ESP_UUID_LEN_128] 		= {0xd7, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t CHARGER_SERV_CHAR_CHARGER_MID_descr[]  = "Device MID";
static uint8_t CHARGER_SERV_CHAR_CHARGER_MID_val[9];

//const uint8_t 	PIN_uuid128[ESP_UUID_LEN_128] 			= {0xd8, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t CHARGER_SERV_CHAR_pin_descr[]  	= "Set PIN";
static uint8_t WIFI_SERV_CHAR_PIN_val[4]        		= {0x30, 0x30, 0x30, 0x30};

const uint8_t 	AvailableWifi_uuid128[ESP_UUID_LEN_128] = {0x01, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t AVAILABLE_WIFI_CHAR_pin_descr[]  	= "Available WiFi Networks";
//static uint8_t AVAILABLE_WIFI_SERV_CHAR_val[400];//      = {'a', 'c', 'e'};

const uint8_t 	NetworkStatus_uuid128[ESP_UUID_LEN_128] = {0x02, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t NETWORK_STATUS_CHAR_pin_descr[]  	= "Network Status";
//static uint8_t NETWORK_STATUS_SERV_CHAR_val[300];//      = {0x00, 0x00, 0x00, 0x00};

const uint8_t 	Auth_uuid128[ESP_UUID_LEN_128] 			= {0x00, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t AUTH_CHAR_pin_descr[]  			= "Auth";
static uint8_t AUTH_SERV_CHAR_val[]        				= {"0"};

const uint8_t 	Save_uuid128[ESP_UUID_LEN_128] 			= {0xd5, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t SAVE_CHAR_pin_descr[]  			= "Save";
static uint8_t SAVE_SERV_CHAR_val[]        				= {"0"};

const uint8_t HmiBrightness_uid128[ESP_UUID_LEN_128] 	= {0x09, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t HMI_BRIGHTNESS_descr[]  		 	= "HMI brightness";
static uint8_t HMI_BRIGHTNESS_val[8]          			= {0x00};

const uint8_t CommunicationMode_uid128[ESP_UUID_LEN_128] = {0xd2, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t COMMUNICATION_MODE_CHAR_descr[]   	= "Communication Mode";
static uint8_t COMMUNICATION_MODE_val[8]          		= {0x00};

const uint8_t FirmwareVersion_uid128[ESP_UUID_LEN_128]  = {0x00, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t FIRMWARE_VERSION_CHAR_descr[]   	= "Firmware Version";
//static uint8_t FIRMWARE_VERSION_val[8]          		= {0x00};

const uint8_t OperationState_uid128[ESP_UUID_LEN_128]  = {0xdc, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};

const uint8_t PairNFCTag_uid128[ESP_UUID_LEN_128] = 	{0xe4, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t PAIR_NFC_TAG_CHAR_descr[]   		= "Pair NFC Tag";
//static uint8_t PAIR_NFC_TAG_val[32]          			= {0x00};


const uint8_t AuthResult_uid128[ESP_UUID_LEN_128] 		= {0xde, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};

const uint8_t OccupiedState_uid128[ESP_UUID_LEN_128] 	= {0xdd, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};

const uint8_t AuthUUID_uid128[ESP_UUID_LEN_128] 		= {0xda, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};


const uint8_t NetworkType_uid128[ESP_UUID_LEN_128] 		= {0x05, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t NETWORK_TYPE_CHAR_descr[]   		= "Network type";
//static uint8_t NETWORK_TYPE_val[4]          			= {0x00};


const uint8_t Standalone_uid128[ESP_UUID_LEN_128] 		= {0xd9, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Standalone_descr[]  			 	= "Standalone";
//static uint8_t Standalone_val[8]          				= {0x00};

const uint8_t Standalone_Phase_uid128[ESP_UUID_LEN_128] = {0x06, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t StandalonePhase_descr[]   			= "Standalone Phase";
//static uint8_t Standalone Phase_val[8]          		= {0x00};

const uint8_t Standalone_Current_uid128[ESP_UUID_LEN_128] = {0x04, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Standalone_Current_descr[]   		= "Standalone Current";
//static uint8_t Standalone_Current_val[8]          		= {0x00};

const uint8_t Permanent_Lock_uid128[ESP_UUID_LEN_128]	= {0x08, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Permanent_Lock_descr[]   			= "Permanent Lock";
//static uint8_t Permanent_Lock_val[1]          			= {0x0};

const uint8_t 	Warnings_uuid128[ESP_UUID_LEN_128] 		= {0x01, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Warnings_descr[]  					= "Warnings";
//static uint8_t Warnings_val[8]        					= {0x00};


const uint8_t Wifi_MAC_uid128[ESP_UUID_LEN_128] 		= {0x05, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Wifi_MAC_descr[]   				= "Wifi MAC";
//static uint8_t Wifi_MAC_val[17]          				= {0x00};

const uint8_t Max_Inst_Current_Switch_uid128[ESP_UUID_LEN_128] 	= {0x06, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Max_Inst_Current_Switch_descr[]   	= "Max Installation Current Switch";
//static uint8_t Max_Inst_Current_Switch_val[8]          	= {0x00};

const uint8_t Max_Inst_Current_Config_uid128[ESP_UUID_LEN_128] = {0x07, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Max_Inst_Current_Config_descr[]   	= "Max Installation Current Config";
//static uint8_t Max_Inst_Current_Config_val[8]          	= {0x00};

const uint8_t Phase_Rotation_uid128[ESP_UUID_LEN_128] 	= {0x08, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t Phase_Rotation_descr[]   			= "Phase Rotation";
//static uint8_t Phase_Rotation_val[1]          			= {0x0};





const uint8_t RunCommand_uid128[ESP_UUID_LEN_128] = 	{0x03, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t RUN_COMMAND_CHAR_descr[]   		= "Run Command";
static uint8_t COMMAND_val[32]          				= {0x00};





//static char wifiPackage[500] = {0}; //TODO: Evaluate size
static char * wifiPackage = NULL;

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
	//[WIFI_SSID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_info_ccc), (uint8_t *)WIFI_SERV_CHAR_info_ccc}},

	[WIFI_PSK_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
    [WIFI_PSK_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &WiFiPSK_uid128, ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
    //[WIFI_PSK_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
    //[WIFI_PSK_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_config_ccc), (uint8_t *)WIFI_SERV_CHAR_config_ccc}},

	[CHARGER_DEVICE_MID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_DEVICE_MID_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},//TODO remove write?
	//[CHARGER_DEVICE_MID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_DEVICE_MID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

//	[CHARGER_PIN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
//	[CHARGER_PIN_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &PIN_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
//	[CHARGER_PIN_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_PIN_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},


	[CHARGER_AVAIL_WIFI_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AVAIL_WIFI_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &AvailableWifi_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_AVAIL_WIFI_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_AVAIL_WIFI_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_NETWORK_STATUS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_NETWORK_STATUS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	//[CHARGER_NETWORK_STATUS_UUID] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	//[CHARGER_NETWORK_STATUS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_NETWORK_STATUS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	//[CHARGER_WARNINGS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	//[CHARGER_WARNINGS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Warnings_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_WARNINGS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_WARNINGS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_AUTH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AUTH_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Auth_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_AUTH_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_AUTH_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_SAVE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_SAVE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Save_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_SAVE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	//[CHARGER_SAVE_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(CHARGER_SERV_CHAR_config_ccc), (uint8_t *)CHARGER_SERV_CHAR_config_ccc}},

	[CHARGER_HMI_BRIGHTNESS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_HMI_BRIGHTNESS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &HmiBrightness_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_HMI_BRIGHTNESS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_COMMUNICATION_MODE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_COMMUNICATION_MODE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &CommunicationMode_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_COMMUNICATION_MODE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_FIRMWARE_VERSION_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_FIRMWARE_VERSION_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &FirmwareVersion_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_FIRMWARE_VERSION_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},


	[CHARGER_OPERATION_STATE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_OPERATION_STATE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &OperationState_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},


	[CHARGER_PAIR_NFC_TAG_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_PAIR_NFC_TAG_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &PairNFCTag_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},


	[CHARGER_AUTHORIZATION_RESULT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AUTHORIZATION_RESULT_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &AuthResult_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},

	[CHARGER_OCCUPIED_STATE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_OCCUPIED_STATE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &OccupiedState_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},

	[CHARGER_AUTH_UUID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_AUTH_UUID_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &AuthUUID_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},



	[CHARGER_NETWORK_TYPE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_NETWORK_TYPE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkType_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_NETWORK_TYPE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},


	[CHARGER_STANDALONE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STANDALONE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_STANDALONE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_STANDALONE_PHASE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STANDALONE_PHASE_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_Phase_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_STANDALONE_PHASE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_STANDALONE_CURRENT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_STANDALONE_CURRENT_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Standalone_Current_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_STANDALONE_CURRENT_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_PERMANENT_LOCK_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_PERMANENT_LOCK_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Permanent_Lock_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_PERMANENT_LOCK_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_WARNINGS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_WARNINGS_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Warnings_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_WARNINGS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_WIFI_MAC_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_WIFI_MAC_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_MAC_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_WIFI_MAC_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_MAX_INST_CURRENT_SWITCH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_MAX_INST_CURRENT_SWITCH_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Max_Inst_Current_Switch_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_MAX_INST_CURRENT_SWITCH_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_MAX_INST_CURRENT_CONFIG_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_MAX_INST_CURRENT_CONFIG_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Max_Inst_Current_Config_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_MAX_INST_CURRENT_CONFIG_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	[CHARGER_PHASE_ROTATION_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_PHASE_ROTATION_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Phase_Rotation_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	//[CHARGER_PHASE_ROTATION_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},


	[CHARGER_RUN_COMMAND_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[CHARGER_RUN_COMMAND_UUID] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &RunCommand_uid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},


};

void charInit()
{
/*#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif*/
}


void PulseLedAtConnect()
{
	MessageType ret = MCU_SendCommandId(CommandIndicateAppConnect);
	if(ret == MsgCommandAck)
	{
		ESP_LOGI(TAG, "MCU CommandIndicateAppConnect OK. ");
	}
	else
	{
		ESP_LOGI(TAG, "MCU CommandIndicateAppConnect FAILED");
	}
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
static char nrTostr[11] = {0};

static bool configSession = false;
static int statusSegmentCount = 0;
char *jsonString = NULL;
cJSON *jsonObject = NULL;


void handleWifiReadEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{

	//First time when unconfigured by switch or app, don't ask for pin.
	if((MCU_GetSwitchState() == 0) && (MCU_ChargeCurrentInstallationMaxLimit() == 0.0))
		configSession = true;

	if(configSession == true)
		AUTH_SERV_CHAR_val[0] = '1';


	//ESP_LOGW(TAG, "**** AUTH: %s, cs: %d ****", AUTH_SERV_CHAR_val, configSession); //For debug

#ifdef USE_PIN
	//Check authentication before allowing most reads. Some exceptions.
	if((AUTH_SERV_CHAR_val[0] == '0') && (attrIndex != CHARGER_DEVICE_MID_UUID) &&
			(attrIndex != CHARGER_FIRMWARE_VERSION_UUID) && (attrIndex != CHARGER_WARNINGS_UUID) &&
			(attrIndex != CHARGER_AUTH_UUID) && (attrIndex != CHARGER_OPERATION_STATE_UUID) &&
			(attrIndex != CHARGER_PAIR_NFC_TAG_UUID) && (attrIndex != CHARGER_AUTHORIZATION_RESULT_UUID) &&
			(attrIndex != CHARGER_NETWORK_STATUS_UUID) && (attrIndex != CHARGER_OCCUPIED_STATE_UUID))
	{
		ESP_LOGE(TAG, "Read: No pin set: %d", attrIndex);
		return;
	}
#endif

	//char jsonString[] = "{\"wifi\":{\"ip\":\"10.0.0.1\",\"link\":-54},\"online\":true}";//"{'{', '"', 'o', 'n', 'l', 'i', 'n', 'e', '"', '=', 't', 'r', 'u', 'e', '}', '\0'};
	//char jsonString[] = "{\"online\":true}";
	//int jsonStringLen = strlen(jsonString);


	if(attrIndex != CHARGER_NETWORK_STATUS_UUID)
		statusSegmentCount = 0;

	static int wifiSegmentCount = 0;
	if(attrIndex != CHARGER_AVAIL_WIFI_UUID)
		wifiSegmentCount = 0;

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
   /* case WIFI_PSK_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
        memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_PSK_descr, sizeof(WIFI_SERV_CHAR_PSK_descr));
        rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_PSK_descr);
        break;

    case WIFI_SSID_DESCR:
	    memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
        memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_SSID_descr, sizeof(WIFI_SERV_CHAR_SSID_descr));
        rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_SSID_descr);
        break;*/


    case CHARGER_DEVICE_MID_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, CHARGER_SERV_CHAR_CHARGER_MID_val, sizeof(CHARGER_SERV_CHAR_CHARGER_MID_val));
		rsp->attr_value.len = sizeof(CHARGER_SERV_CHAR_CHARGER_MID_val);
		break;

    /*case CHARGER_DEVICE_MID_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, CHARGER_SERV_CHAR_CHARGER_MID_descr, sizeof(CHARGER_SERV_CHAR_CHARGER_MID_descr));
		rsp->attr_value.len = sizeof(CHARGER_SERV_CHAR_CHARGER_MID_descr);
		break;*/

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

    /*case CHARGER_AUTH_DESCR:
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
		break;*/

    case CHARGER_AVAIL_WIFI_UUID:

    	if(wifiSegmentCount == 0)
    	{
    		apNr = 0;
			nrOfWifiSegments = 0;
			wifiRemainder = 0;
    		//memset(wifiPackage, 0, 500);
			wifiPackage = calloc(600, 1);

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

			if(wifiSegmentCount == 0)
			{

				wifiPackage[0] = 0;	//version
				wifiPackage[1] = apNr; //Nr of discovered access points

				int nextIndex = 2;
				int i;
				for (i = 0; i < apNr; i++)
				{
					int j;
					bool duplicateDetected = false;
					for(j = 0; j<i; j++)
					{
						if(memcmp(ap_records[i].ssid, ap_records[j].ssid, 32) == 0)
						{
							ESP_LOGE(TAG, "Duplicate: %s, i=%d, j=%d", ap_records[i].ssid, i,j);
							duplicateDetected = true;
						}
					}
					if(duplicateDetected == true)
						continue;

					wifiPackage[nextIndex++] = ap_records[i].rssi;

					//Try always require password to avoid wifi-lib problem with WPA2/WPA3-Personal APs
					/*if(ap_records[i].authmode == WIFI_AUTH_OPEN)
						wifiPackage[nextIndex++] = 0;
					else*/
					wifiPackage[nextIndex++] = 1;

					//Add SSID length to package
					int ssidLen = strlen((char*)ap_records[i].ssid);
					if (ssidLen <= 32)
						wifiPackage[nextIndex++] = ssidLen;

					//Add SSID name to package
					memcpy(&wifiPackage[nextIndex], &ap_records[i].ssid, ssidLen);
					nextIndex += ssidLen;

				}

				//int MTUsize = 22;
				nrOfWifiSegments = nextIndex/MTUsize;
				wifiRemainder = nextIndex % MTUsize;
			}

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

				free(wifiPackage);
				//network_WifiScanEnd();
				ESP_LOGW(TAG, "Resume current wifi");
				network_updateWifi();
			}

		}
    	break;

    case CHARGER_NETWORK_STATUS_UUID:

		//{"wifi":{"ip":"10.0.0.1","link":-54},"online":true}

    	if(statusSegmentCount == 0)
    	{


			jsonObject = cJSON_CreateObject();


			if(storage_Get_CommunicationMode() == eCONNECTION_NONE)
			{
				ESP_LOGI(TAG, "IP4 Address: 0.0.0.0");
				cJSON_AddItemToObject(jsonObject, "wifi", wifiObject=cJSON_CreateObject());
				cJSON_AddStringToObject(wifiObject, "ip", "0.0.0.0");
				cJSON_AddNumberToObject(wifiObject, "link", 0);
				cJSON_AddBoolToObject(jsonObject, "online", false);

			}
			else if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
			{
				ESP_LOGI(TAG, "IP4 Address: %s", network_GetIP4Address());
				cJSON_AddItemToObject(jsonObject, "wifi", wifiObject=cJSON_CreateObject());
				cJSON_AddStringToObject(wifiObject, "ip", network_GetIP4Address());
				cJSON_AddNumberToObject(wifiObject, "link", (int)network_WifiSignalStrength());
				cJSON_AddBoolToObject(jsonObject, "online", network_WifiIsConnected());
			}
			else if(storage_Get_CommunicationMode() == eCONNECTION_LTE)
			{
				ESP_LOGI(TAG, "IP4 Address: %s", pppGetIp4Address());
				cJSON_AddItemToObject(jsonObject, "lte", wifiObject=cJSON_CreateObject());
				cJSON_AddStringToObject(wifiObject, "ip", pppGetIp4Address());
				cJSON_AddNumberToObject(wifiObject, "link", (int)GetCellularQuality());
				cJSON_AddBoolToObject(jsonObject, "online", LteIsConnected());
			}

			//jsonString = cJSON_Print(jsonObject);
			jsonString = cJSON_PrintUnformatted(jsonObject);
    	}

    	//Split into segments since iPhone only reads two bytes at the time
		int jsonStringLen = strlen(jsonString);
		int nrOfSegments = jsonStringLen/22;
		int segmentRemainder = jsonStringLen % 22;

		if (statusSegmentCount <= nrOfSegments - 1)
		{
			memcpy(rsp->attr_value.value, &jsonString[22*statusSegmentCount], 22);
			rsp->attr_value.offset = 22*statusSegmentCount;
			rsp->attr_value.len = 22;
			statusSegmentCount++;
		}
		else if (statusSegmentCount == nrOfSegments)
		{
			memcpy(rsp->attr_value.value, &jsonString[22*statusSegmentCount], segmentRemainder);
			rsp->attr_value.offset = 22*statusSegmentCount;
			rsp->attr_value.len = segmentRemainder;
			statusSegmentCount = 0;

			//Clean up heap items
			free(jsonString);
			cJSON_Delete(jsonObject);
		}

    	break;

    case CHARGER_AUTH_UUID:
		rsp->attr_value.value[0] = AUTH_SERV_CHAR_val[0];
		rsp->attr_value.len = 1;
		ESP_LOGI(TAG, "AUTH: %s, %i ",(char*)AUTH_SERV_CHAR_val, AUTH_SERV_CHAR_val[0]);
    	break;

    case CHARGER_SAVE_UUID:
    	rsp->attr_value.value[0] = SAVE_SERV_CHAR_val[0];

    	ESP_LOGW(TAG, " **** SAVE: %s, %i ****",(char*)SAVE_SERV_CHAR_val, SAVE_SERV_CHAR_val[0]);

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

    /*case CHARGER_HMI_BRIGHTNESS_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, HMI_BRIGHTNESS_descr, sizeof(HMI_BRIGHTNESS_descr));
		rsp->attr_value.len = sizeof(HMI_BRIGHTNESS_descr);
		break;*/

    case CHARGER_COMMUNICATION_MODE_UUID:

		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		if(storage_Get_CommunicationMode() == eCONNECTION_WIFI)
		{
			memcpy(COMMUNICATION_MODE_val, "Wifi",4);
			memcpy(rsp->attr_value.value, COMMUNICATION_MODE_val, sizeof(COMMUNICATION_MODE_val));
			rsp->attr_value.len = 4;
			ESP_LOGI(TAG, "Read Wifi");
		}
		else if (storage_Get_CommunicationMode() == eCONNECTION_LTE)
		{
			memcpy(COMMUNICATION_MODE_val, "LTE",3);//TODO: Change to LTE?
			memcpy(rsp->attr_value.value, COMMUNICATION_MODE_val, sizeof(COMMUNICATION_MODE_val));
			rsp->attr_value.len = 3;
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

    /*case CHARGER_COMMUNICATION_MODE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, COMMUNICATION_MODE_CHAR_descr, sizeof(COMMUNICATION_MODE_CHAR_descr));
		rsp->attr_value.len = sizeof(COMMUNICATION_MODE_CHAR_descr);
		break;*/

    case CHARGER_FIRMWARE_VERSION_UUID:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		int swlen = strlen(GetSoftwareVersion());
 		memcpy(rsp->attr_value.value, GetSoftwareVersion(), swlen);
 		rsp->attr_value.len = swlen;
 		break;

    case CHARGER_OPERATION_STATE_UUID:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

 		char operationState[4] = {0};

 		sprintf(operationState, "%d", MCU_GetChargeOperatingMode());
 		ESP_LOGI(TAG, "Reading Operation State: %s, len: %d", operationState, strlen(operationState));

 		memcpy(rsp->attr_value.value, &operationState, strlen(operationState));
 		rsp->attr_value.len = strlen(operationState);
 		break;

    case CHARGER_PAIR_NFC_TAG_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		char NFCPairState[2] = {0};//'0';

		rfidPairing_GetStateAsChar(NFCPairState);

		memcpy(rsp->attr_value.value, NFCPairState, 1);
		rsp->attr_value.len = 1;

		bool hasSetInactiveState = rfidPairing_ClearState();
		if(hasSetInactiveState == true)
		{
	    	MCU_StopLedOverride();
		}
		break;

    case CHARGER_OCCUPIED_STATE_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		char occupiedState = '0';

		if((MCU_GetChargeOperatingMode() != 1) && chargeSession_IsAuthenticated())
		{
			occupiedState = '1';
		}

		ESP_LOGI(TAG, "Reading occupiedState: %c", occupiedState);

		memcpy(rsp->attr_value.value, &occupiedState, 1);
		rsp->attr_value.len = 1;
		break;

    case CHARGER_AUTHORIZATION_RESULT_UUID:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	char authorizationResult[41] = {0};

    	//Return empty string
    	if(rfidPairing_GetState() == ePairing_Reading)
    	{
    		ESP_LOGI(TAG, "Reading Authorization result: %s", authorizationResult);
    		memcpy(rsp->attr_value.value, &authorizationResult, 1);
    		rsp->attr_value.len = 1;
    		break;
    	}
    	else if(rfidPairing_GetState() == ePairing_Inactive)
    	{
			authorizationResult[0] = '0';
			ESP_LOGI(TAG, "Reading Authorization result: %s", authorizationResult);

			memcpy(rsp->attr_value.value, &authorizationResult, 1);
			rsp->attr_value.len = 1;
			break;
    	}


		ESP_LOGI(TAG, "Reading Authorization result: %s", authorizationResult);

		memcpy(rsp->attr_value.value, &authorizationResult, 1);
		rsp->attr_value.len = 1;

    	break;

    /*case CHARGER_FIRMWARE_VERSION_DESCR:
     	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		memcpy(rsp->attr_value.value, FIRMWARE_VERSION_CHAR_descr, sizeof(FIRMWARE_VERSION_CHAR_descr));
 		rsp->attr_value.len = sizeof(FIRMWARE_VERSION_CHAR_descr);
 		break;*/


    case CHARGER_NETWORK_TYPE_UUID:

 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		//memcpy(NETWORK_TYPE_val, MCU_GetGridType(),4);
		memcpy(rsp->attr_value.value, MCU_GetGridTypeString(), 4);
		rsp->attr_value.len = 4;
		ESP_LOGI(TAG, "Read Network type");

 		break;

    /*case CHARGER_NETWORK_TYPE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, NETWORK_TYPE_CHAR_descr, sizeof(NETWORK_TYPE_CHAR_descr));
		rsp->attr_value.len = sizeof(NETWORK_TYPE_CHAR_descr);
		break;*/

    case CHARGER_STANDALONE_UUID:

    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	ESP_LOGI(TAG, "Read Standalone %d ", storage_Get_Standalone());

    	if(storage_Get_Standalone() == 0)
    	{
			memcpy(rsp->attr_value.value, "system", 6);
			rsp->attr_value.len = 6;
    	}
    	else if(storage_Get_Standalone() == 1)
    	{
			memcpy(rsp->attr_value.value, "standalone", 10);
			rsp->attr_value.len = 10;
    	}

		break;

    /*case CHARGER_STANDALONE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Standalone_descr, sizeof(Standalone_descr));
		rsp->attr_value.len = sizeof(Standalone_descr);
		break;*/


    case CHARGER_STANDALONE_PHASE_UUID:

 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	ESP_LOGI(TAG, "Read Standalone Phase %d ", storage_Get_StandalonePhase());

    	memset(nrTostr, 0, sizeof(nrTostr));
    	itoa(storage_Get_StandalonePhase(), nrTostr, 10);

    	memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

 		break;

     /*case CHARGER_STANDALONE_PHASE_DESCR:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
 		memcpy(rsp->attr_value.value, StandalonePhase_descr, sizeof(StandalonePhase_descr));
 		rsp->attr_value.len = sizeof(StandalonePhase_descr);
 		break;*/


     case CHARGER_STANDALONE_CURRENT_UUID:

		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		//In order to be responsive enough we need to read local value, not bounced from MCU
		//ESP_LOGI(TAG, "Read Standalone Current %f ", MCU_StandAloneCurrent());
		//ESP_LOGI(TAG, "Read Standalone Current %f ", storage_Get_StandaloneCurrent());


		float standaloneCurrent = 0.0;
		ZapMessage rxMsg = MCU_ReadParameter(StandAloneCurrent);
		if((rxMsg.length == 4) && (rxMsg.identifier == StandAloneCurrent))
		{
			standaloneCurrent = GetFloat(rxMsg.data);
			ESP_LOGW(TAG, "Read Standalone current %f A", standaloneCurrent);
		}
		else
		{
			ESP_LOGE(TAG, "Read Standalone current failed: %f", standaloneCurrent);
		}

		memset(nrTostr, 0, sizeof(nrTostr));
		//sprintf(nrTostr, "%.1f", storage_Get_StandaloneCurrent());
		sprintf(nrTostr, "%.1f", standaloneCurrent);

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

     /*case CHARGER_STANDALONE_CURRENT_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Standalone_Current_descr, sizeof(Standalone_Current_descr));
  		rsp->attr_value.len = sizeof(Standalone_Current_descr);
  		break;*/

     case CHARGER_PERMANENT_LOCK_UUID:

 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

 		ESP_LOGI(TAG, "Read Permanent Lock %d ", storage_Get_PermanentLock());
 		memset(nrTostr, 0, sizeof(nrTostr));
 		itoa(storage_Get_PermanentLock(), nrTostr, 10);

 		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
 		rsp->attr_value.len = strlen(nrTostr);

 		break;

     /*case CHARGER_PERMANENT_LOCK_DESCR:
   		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
   		memcpy(rsp->attr_value.value, Permanent_Lock_descr, sizeof(Permanent_Lock_descr));
   		rsp->attr_value.len = sizeof(Permanent_Lock_descr);
   		break;*/

     case CHARGER_WARNINGS_UUID:

  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

  		volatile uint32_t warning = MCU_GetWarnings();
  		ESP_LOGI(TAG, "Read Warning 0x%x ", warning);

  		// Each bit in the warning value is transferred separate bytes
  		// 0x00000003 -> 0x02 0x01
  		// Length 0 = No warnings set

  		uint8_t warningBytes[32] = {0};
  		int nextBitShift = 0;
  		int byteNr;
  		int byteCount = 0;
  		for (byteNr = 0; byteNr < 32; byteNr++)
  		{
  			if((warning >> nextBitShift) & 0x1)
  			{
  				warningBytes[byteCount] = byteNr;
  				byteCount++;
  			}
  			nextBitShift++;
  		}

  		memcpy(rsp->attr_value.value, &warningBytes, byteCount);
  		rsp->attr_value.len = byteCount;

  		break;

	/*case CHARGER_WARNINGS_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Warnings_descr, sizeof(Warnings_descr));
		rsp->attr_value.len = sizeof(Warnings_descr);
		break;*/

    case CHARGER_WIFI_MAC_UUID:
 		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

 		uint8_t wifiMAC[18] = {0};
 		esp_read_mac(wifiMAC, 0); //0=Wifi station
 		sprintf((char*)wifiMAC, "%02x:%02x:%02x:%02x:%02x:%02x", wifiMAC[0],wifiMAC[1],wifiMAC[2],wifiMAC[3],wifiMAC[4],wifiMAC[5]);

 		ESP_LOGW(TAG, "Read Wifi MAC: %s, len %d", (char*)wifiMAC, strlen((char*)wifiMAC));

 		memcpy(rsp->attr_value.value, wifiMAC, 17);
 		rsp->attr_value.len = 17;

 		break;

	/*case CHARGER_WIFI_MAC_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, Wifi_MAC_descr, sizeof(Wifi_MAC_descr));
		rsp->attr_value.len = sizeof(Wifi_MAC_descr);
		break;*/

    case CHARGER_MAX_INST_CURRENT_SWITCH_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		ESP_LOGI(TAG, "Read Max installation current SWITCH %f A", MCU_GetMaxInstallationCurrentSwitch());



		memset(nrTostr, 0, sizeof(nrTostr));
		sprintf(nrTostr, "%.1f", MCU_GetMaxInstallationCurrentSwitch());

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

    /*case CHARGER_MAX_INST_CURRENT_SWITCH_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Max_Inst_Current_Switch_descr, sizeof(Max_Inst_Current_Switch_descr));
  		rsp->attr_value.len = sizeof(Max_Inst_Current_Switch_descr);
  		break;*/

    case CHARGER_MAX_INST_CURRENT_CONFIG_UUID:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

    	//In order to be responsive enough we need to read local value instead of value bounced from MCU
		//ESP_LOGI(TAG, "Read Max installation current CONFIG %f A", MCU_ChargeCurrentInstallationMaxLimit());
		//ESP_LOGI(TAG, "Read Max installation current CONFIG %f A", storage_Get_MaxInstallationCurrentConfig());

		float maxInst = 0.0;
		ZapMessage rxMsgm = MCU_ReadParameter(ChargeCurrentInstallationMaxLimit);
		if((rxMsgm.length == 4) && (rxMsgm.identifier == ChargeCurrentInstallationMaxLimit))
		{
			maxInst = GetFloat(rxMsgm.data);
			ESP_LOGW(TAG, "Read Max installation current CONFIG %f A", maxInst);
		}
		else
		{
			ESP_LOGE(TAG, "Read Max installation current CONFIG failed: %f", maxInst);
		}

		if((maxInst <= 32.0) && (storage_Get_MaxInstallationCurrentConfig() > 32.0) && (storage_Get_MaxInstallationCurrentConfig() <= 40.0))
			maxInst = storage_Get_MaxInstallationCurrentConfig();

		memset(nrTostr, 0, sizeof(nrTostr));
		//sprintf(nrTostr, "%.1f", storage_Get_MaxInstallationCurrentConfig());
		sprintf(nrTostr, "%.1f", maxInst);

		memcpy(rsp->attr_value.value, nrTostr, strlen(nrTostr));
		rsp->attr_value.len = strlen(nrTostr);

		break;

    /*case CHARGER_MAX_INST_CURRENT_CONFIG_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Max_Inst_Current_Config_descr, sizeof(Max_Inst_Current_Config_descr));
  		rsp->attr_value.len = sizeof(Max_Inst_Current_Config_descr);
  		break;*/

    case CHARGER_PHASE_ROTATION_UUID:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));

		uint8_t phaseRotation = storage_Get_PhaseRotation();
		ESP_LOGI(TAG, "Read PhaseRotation %d ", phaseRotation);

		memcpy(rsp->attr_value.value, &phaseRotation, sizeof(uint8_t));
		rsp->attr_value.len = sizeof(uint8_t);

		break;


    /*case CHARGER_PHASE_ROTATION_DESCR:
  		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
  		memcpy(rsp->attr_value.value, Phase_Rotation_descr, sizeof(Phase_Rotation_descr));
  		rsp->attr_value.len = sizeof(Phase_Rotation_descr);
  		break;*/
    }
}

static bool saveWifi = false;
static bool saveConfiguration = false;
static enum CommunicationMode interface = eCONNECTION_NONE;
static enum CommunicationMode previousInterface = eCONNECTION_NONE;

void handleWifiWriteEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{

	//First time when unconfigured by switch or app, don't ask for pin.
	//if((MCU_GetSwitchState() == 0) && (MCU_ChargeCurrentInstallationMaxLimit() == 0.0))
	if(configSession == true)
		AUTH_SERV_CHAR_val[0] = '1';

#ifdef USE_PIN
	//Check authentication before allowing writes
	if((AUTH_SERV_CHAR_val[0] == 0) && (attrIndex != CHARGER_AUTH_UUID)	&& (attrIndex != CHARGER_PAIR_NFC_TAG_UUID) && (attrIndex != CHARGER_AUTH_UUID_UUID))
	{
		ESP_LOGE(TAG, "Write: No pin set: %d", attrIndex);
		return;
	}
#endif

    switch( attrIndex )
    {


    case CHARGER_AUTH_UUID_UUID:

    	if((rfidPairing_GetState() == ePairing_Inactive) && (NFCGetTagInfo().tagIsValid == false))
    	{
    		char bleId[50] = {0};
    		strcpy(bleId, "ble-");
    		memcpy(bleId+4, param->write.value, param->write.len);
    		ESP_LOGW(TAG, "### BLE-UUID: %s ###", bleId);

    		if((isMqttConnected()) && (storage_Get_Standalone() == false))
    		{

    			MessageType ret = MCU_SendUint8Parameter(ParamAuthState, SESSION_AUTHORIZING);
				if(ret == MsgWriteAck)
					ESP_LOGI(TAG, "Ack on SESSION_AUTHORIZING");
				else
					ESP_LOGW(TAG, "NACK on SESSION_AUTHORIZING!!!");

    			ESP_LOGW(TAG, "Setting BLE pending, waithing for cloud authentication");
    			SetPendingRFIDTag(bleId);
    			publish_debug_telemetry_observation_NFC_tag_id(bleId);
    		}
    		else
    		{
    			ESP_LOGW(TAG, "Local authentication, setting to chargeSession if Ok");
    			uint8_t match = authentication_CheckBLEId(bleId);
    			if(match == 1)
    			{
    				chargeSession_SetAuthenticationCode(bleId);
    			}
    		}
    	}

    	if((rfidPairing_GetState() == ePairing_Reading) && (NFCGetTagInfo().tagIsValid == false))
    	{
    		if(param->write.len == 36)
			{
    			rfidPairing_SetNewUserId(param->write.value, param->write.len);
			}
			else
			{
				ESP_LOGI(TAG, "Incorrect Auth UUID length %d", param->write.len);
			}
    	}

		break;


    case WIFI_PSK_UUID:
        /*
         *  Handle any writes to Wifi Info char here
         */

        // This prints the first byte written to the characteristic
        ESP_LOGI(TAG, "Wifi Info characteristic received with %02x", param->write.value[0]);


        if(param->write.len <= 64)
        {
        	memset(WIFI_SERV_CHAR_PSK_val,0, sizeof(WIFI_SERV_CHAR_PSK_val));
        	memcpy(WIFI_SERV_CHAR_PSK_val,param->write.value, param->write.len);
        	ESP_LOGI(TAG, "New Wifi PSK %s", WIFI_SERV_CHAR_PSK_val);

        	saveWifi = true;
        	SAVE_SERV_CHAR_val[0] = '0';
        }
        else
        	ESP_LOGE(TAG, "To long Wifi PSK %s", WIFI_SERV_CHAR_PSK_val);

//		storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);
//
//		//Make the values
//		network_CheckWifiParameters();
	    break;

    case WIFI_SSID_UUID:
        /*
         *  Handle any writes to Wifi Val char here
         */
    	ESP_LOGI(TAG, "Wifi Val characteristic received with %02x", param->write.value[0]);
    	memset(WIFI_SERV_CHAR_SSID_val,0, sizeof(WIFI_SERV_CHAR_SSID_val));
    	if((32 >= param->write.len) && (param->write.len > 0))
    	{
    		memcpy(WIFI_SERV_CHAR_SSID_val,param->write.value, param->write.len);
    		ESP_LOGI(TAG, "New Wifi SSID %s", WIFI_SERV_CHAR_SSID_val);

    		saveWifi = true;
    		SAVE_SERV_CHAR_val[0] = '0';
    	}
    	else
    		ESP_LOGE(TAG, "To long Wifi SSID %s", WIFI_SERV_CHAR_SSID_val);

	    break;


    case CHARGER_HMI_BRIGHTNESS_UUID:

    	ESP_LOGI(TAG, "HMI brightness characteristic received with %02x", param->write.value[0]);

    	memset(HMI_BRIGHTNESS_val,0, sizeof(HMI_BRIGHTNESS_val));
		memcpy(HMI_BRIGHTNESS_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New hmiBrightness %s", HMI_BRIGHTNESS_val);

		float hmiBrightness = atof((char*)HMI_BRIGHTNESS_val);

		if((1.0 >= hmiBrightness) && (hmiBrightness >= 0.0 ))
		{
			MessageType ret = MCU_SendFloatParameter(HmiBrightness, hmiBrightness);
			if(ret == MsgWriteAck)
			{
				storage_Set_HmiBrightness(hmiBrightness);
				ESP_LOGI(TAG, "Set hmiBrightness: %f", hmiBrightness);
				storage_SaveConfiguration();
			}
		}

   		break;

    case CHARGER_COMMUNICATION_MODE_UUID:

    	ESP_LOGI(TAG, "Wifi Val characteristic received with %02x", param->write.value[0]);

    	memset(COMMUNICATION_MODE_val,0, sizeof(COMMUNICATION_MODE_val));
		memcpy(COMMUNICATION_MODE_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Communication Mode %s", COMMUNICATION_MODE_val);

		//enum CommunicationMode interface = eCONNECTION_NONE;

		if(strncmp("Wifi", (char*)COMMUNICATION_MODE_val, 4) == 0)
		{
			interface = eCONNECTION_WIFI;
			ESP_LOGI(TAG, "Set Wifi");

		}
		else if(strncmp("LTE", (char*)COMMUNICATION_MODE_val, 3) == 0)
		{
			interface = eCONNECTION_LTE;
			ESP_LOGI(TAG, "Set LTE");
		}
		else
		{
			interface = eCONNECTION_NONE;
			ESP_LOGI(TAG, "Set None");
		}

		previousInterface = storage_Get_CommunicationMode();
		storage_Set_CommunicationMode(interface);
		connectivity_ActivateInterface(interface);

		SAVE_SERV_CHAR_val[0] = '0';

		//saveConfiguration = true;

   		break;


    case CHARGER_PAIR_NFC_TAG_UUID:

    	ESP_LOGI(TAG, "Pair NFC tag name received: %s", param->write.value);
    	if(param->write.len >= 26)
    	{
    		ESP_LOGE(TAG, "To long NFC tag string");
    		break;
    	}

    	NFCClearTag();
    	rfidPairing_ClearBuffers();
    	rfidPairing_SetNewTagName(param->write.value, param->write.len);
    	rfidPairing_SetState(ePairing_Reading);

    	MCU_StartLedOverride();



		//ESP_LOGI(TAG, "New NFC tag string %s", PAIR_NFC_TAG_val);

   		break;


    case CHARGER_STANDALONE_UUID:

    	ESP_LOGI(TAG, "Standalone received %02x", param->write.value[0]);

    	uint8_t standalone = 0xff;
    	char * systemStr = "system";
    	char * standaloneStr = "standalone";

    	if(memcmp(param->write.value, systemStr, 6) == 0)
    		standalone = 0;

    	else if(memcmp(param->write.value, standaloneStr, 10) == 0)
    		standalone = 1;

    	if((standalone == 0) || (standalone == 1))
		{
			MessageType ret = MCU_SendUint8Parameter(ParamIsStandalone, (uint8_t)standalone);
			if(ret == MsgWriteAck)
			{
				storage_Set_Standalone((uint8_t)standalone);
				ESP_LOGI(TAG, "DoSave 712 standalone=%d\n", standalone);
				saveConfiguration = true;
				SAVE_SERV_CHAR_val[0] = '0';
			}
			else
			{
				ESP_LOGE(TAG, "MCU standalone parameter error");
			}
		}
		else
		{
			ESP_LOGI(TAG, "Invalid standalone: %d \n", standalone);
		}

   		break;

    case CHARGER_STANDALONE_PHASE_UUID:

    	ESP_LOGI(TAG, "Standalone PHASE received %02x", param->write.value[0]);

    	memset(nrTostr, 0, sizeof(nrTostr));
    	memcpy(nrTostr, param->write.value, param->write.len);
    	uint8_t standalonePhase = (uint8_t)atoi(nrTostr);

    	//Sanity check
    	if((10 >= standalonePhase) && (standalonePhase >= 1))
    	{
    		storage_Set_StandalonePhase(standalonePhase);
    		ESP_LOGI(TAG, "Set standalone Phase: %d", standalonePhase);
    		saveConfiguration = true;
    		SAVE_SERV_CHAR_val[0] = '0';
    	}

   		break;
    case CHARGER_STANDALONE_CURRENT_UUID:

    	//ESP_LOGI(TAG, "Standalone PHASE written %02x", param->write.value[0]);

    	memset(nrTostr, 0, sizeof(nrTostr));
    	memcpy(nrTostr, param->write.value, param->write.len);
    	float standaloneCurrent = atof(nrTostr);

    	ESP_LOGI(TAG, "Standalone CURRENT received %f", standaloneCurrent);

    	//Sanity check
    	if((32.0 >= standaloneCurrent) && (standaloneCurrent >= 6.0))
    	{
    		MessageType ret = MCU_SendFloatParameter(StandAloneCurrent, standaloneCurrent);
			if(ret == MsgWriteAck)
			{
				storage_Set_StandaloneCurrent(standaloneCurrent);
				ESP_LOGI(TAG, "Set standalone Current to MCU: %f", standaloneCurrent);
				storage_SaveConfiguration();
			}
			else
			{
				ESP_LOGE(TAG, "MCU standalone current parameter error");
			}
    	}

   		break;

    case CHARGER_PERMANENT_LOCK_UUID:

    	ESP_LOGI(TAG, "Permanent lock received %02x", param->write.value[0]);

    	uint8_t lockValue = 0xFF;

    	if(param->write.value[0] == '0')
    		lockValue = 0;
    	else if (param->write.value[0] == '1')
    		lockValue = 1;

    	if((lockValue == 0) || (lockValue == 1))
		{
			MessageType ret = MCU_SendUint8Parameter(PermanentCableLock, lockValue);
			if(ret == MsgWriteAck)
			{
				storage_Set_PermanentLock(lockValue);

				ESP_LOGI(TAG, "BLE PermanentLock=%d sent to MCU", lockValue);

				storage_SaveConfiguration();
			}
			else
			{
				ESP_LOGE(TAG, "BLE->MCU ParamPermanentCableLock parameter error");
			}
		}
		else
		{
			ESP_LOGI(TAG, "BLE Invalid lockValue: %d \n", lockValue);
		}

   		break;

    case CHARGER_MAX_INST_CURRENT_CONFIG_UUID:

    	memset(nrTostr, 0, sizeof(nrTostr));
    	memcpy(nrTostr, param->write.value, param->write.len);
    	float maxInstCurrConfig = atof(nrTostr);

    	ESP_LOGI(TAG, "Max installation current CONFIG received %f", maxInstCurrConfig);

    	//Sanity check
    	if((40.0 >= maxInstCurrConfig) && (maxInstCurrConfig >= 6.0))
    	{
    		float limitedMaxInstCurrent = maxInstCurrConfig;

    		//If installed on a 40 A fuse reduce the value sent to MCU.
			if(limitedMaxInstCurrent > 32.0)
				limitedMaxInstCurrent = 32.0;

    		MessageType ret = MCU_SendFloatParameter(ChargeCurrentInstallationMaxLimit, limitedMaxInstCurrent);
			if(ret == MsgWriteAck)
			{
				//Store up to 40A value
				storage_Set_MaxInstallationCurrentConfig(maxInstCurrConfig);
				ESP_LOGI(TAG, "Set MaxInstallationCurrentConfig to MCU: %f", maxInstCurrConfig);
				storage_SaveConfiguration();
			}
    	}

   		break;

    case CHARGER_PHASE_ROTATION_UUID:

    	ESP_LOGI(TAG, "PhaseRotation received %02x", param->write.value[0]);
    	uint8_t phaseRotation = param->write.value[0];
    	//memset(nrTostr, 0, sizeof(nrTostr));
    	//memcpy(nrTostr, param->write.value, param->write.len);
    	//uint8_t phaseRotation = (uint8_t)atoi(nrTostr);

    	//Sanity check
    	if(18 >= phaseRotation)
    	{
    		storage_Set_PhaseRotation(phaseRotation);
    		ESP_LOGI(TAG, "Set phaseRotation: %d", phaseRotation);
    		storage_SaveConfiguration();//Not using save button in app
    	}

   		break;


    case CHARGER_RUN_COMMAND_UUID:

    	ESP_LOGW(TAG, " ****** Run Command received with %s ********", param->write.value);
    	if(param->write.len >= 32)
    	{
    		ESP_LOGE(TAG, "To long command string");
    		break;
    	}

    	memset(COMMAND_val,0, sizeof(COMMAND_val));
		memcpy(COMMAND_val,param->write.value, param->write.len);

		int command = atoi((char*)COMMAND_val);
		if(command == CommandUpgradeFirmware)
		{
    	    //Blink with LED
			ESP_LOGI(TAG, "MCU CommandIndicateOk command");
			MessageType ret = MCU_SendCommandId(CommandIndicateOk);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU CommandIndicateOk OK. ");
			}
			else
			{
				ESP_LOGI(TAG, "MCU CommandIndicateOk FAILED");
			}

			ESP_LOGI(TAG, "Update firmware command %i", command);

			start_segmented_ota_if_new_version();


		}
		else if(command == CommandSwReboot)
		{
			ESP_LOGI(TAG, "SW reboot command");
			MessageType ret = MCU_SendCommandId(CommandReset);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU Start command OK. Restarting ESP...");

				storage_Set_And_Save_DiagnosticsLog("#5 BLE restart - OK");

				esp_restart();
			}
			else
			{

				ESP_LOGI(TAG, "MCU Start command FAILED");
			}
		}
		else if(command == CommandDisableCertificateOnce)
		{
			storage_Set_DiagnosticsMode(eDISABLE_CERTIFICATE_ONCE);
			storage_SaveConfiguration();
		}
		else if(command == CommandDisableCertificateAlways)
		{
			storage_Set_DiagnosticsMode(eDISABLE_CERTIFICATE_ALWAYS);
			storage_SaveConfiguration();
		}

		//else if(command == CommandFactoryReset)
		else
			ESP_LOGI(TAG, "BLE command not supported: %i", command);

   		break;


    case CHARGER_AUTH_UUID:

		ESP_LOGI(TAG, "Adapter pin %02x", param->write.value[0]);

		if((param->write.value[0] == WIFI_SERV_CHAR_PIN_val[0]) &&
				(param->write.value[1] == WIFI_SERV_CHAR_PIN_val[1]) &&
				(param->write.value[2] == WIFI_SERV_CHAR_PIN_val[2]) &&
				(param->write.value[3] == WIFI_SERV_CHAR_PIN_val[3]))
		{

			///memset(WIFI_SERV_CHAR_PIN_val,0, sizeof(WIFI_SERV_CHAR_PIN_val));	//TODO check how this works
			///memcpy(WIFI_SERV_CHAR_PIN_val,param->write.value, param->write.len);

			pinRetryCounter = 0;
			AUTH_SERV_CHAR_val[0] = '1';


			//Blink with LED
			ESP_LOGI(TAG, "App connected command");
			MessageType ret = MCU_SendCommandId(CommandIndicateAppConnect);
			if(ret == MsgCommandAck)
			{
				ESP_LOGI(TAG, "MCU App connected command OK. ");
			}
			else
			{
				ESP_LOGI(TAG, "MCU App connected command FAILED");
			}

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

		//First time when unconfigured by switch or app, don't ask for pin.
		//if((MCU_GetSwitchState() == 0) && (MCU_ChargeCurrentInstallationMaxLimit() == 0.0))
		if(configSession == true)
			AUTH_SERV_CHAR_val[0] = '1';

	#ifndef USE_PIN
		AUTH_SERV_CHAR_val[0] = '1'; //Add definition to disable pin
	#endif
		ESP_LOGI(TAG, "Auth value %s", AUTH_SERV_CHAR_val);

		break;

    case CHARGER_SAVE_UUID:
		ESP_LOGI(TAG, "Save value %02x", param->write.value[0]);

    	///wasValid = network_wifiIsValid();

		SAVE_SERV_CHAR_val[0] = param->write.value[0];

		if((param->write.value[0] == '1') && (saveWifi == true))
		{
			storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);

			//Make the values active
			wasValid = network_CheckWifiParameters();

			if(wasValid == true)
				network_updateWifi();

			saveWifi = false;
		}


		if((param->write.value[0] == '1') && (saveConfiguration == true))
		{
			storage_SaveConfiguration();

			saveConfiguration = false;
		}

		//param->write.value[0] = 0;
		SAVE_SERV_CHAR_val[0] = '1';

		///if(wasValid == true)
			///network_updateWifi();

		ESP_LOGI(TAG, "Save val %c->%c", param->write.value[0], SAVE_SERV_CHAR_val[0]);

		if((previousInterface != eCONNECTION_WIFI) && (interface == eCONNECTION_WIFI))
		{
			storage_SaveConfiguration();
			ESP_LOGW(TAG, "LTE -> WIFI restart");
			esp_restart();
		}

		if((previousInterface != eCONNECTION_LTE) && (interface == eCONNECTION_LTE))
		{
			storage_SaveConfiguration();
			ESP_LOGW(TAG, "WIFI -> LTE restart");
			esp_restart();
		}

		if((previousInterface != eCONNECTION_NONE) && (interface == eCONNECTION_NONE))
		{
			storage_SaveConfiguration();
			ESP_LOGW(TAG, "eCONNECTION_NONE saved");
		}

		break;
	}

}


void ClearAuthValue()
{
		AUTH_SERV_CHAR_val[0] = '0';
		SAVE_SERV_CHAR_val[0] = '0';
		configSession = false;
		ESP_LOGW(TAG, "Cleared Auth");

		statusSegmentCount = 0;
		rfidPairing_SetState(ePairing_Inactive);
		MCU_StopLedOverride();
}

