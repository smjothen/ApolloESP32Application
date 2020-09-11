
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
//static const uint16_t ADAPTER_SERV_CHAR_config_uuid    = 0xABCD;

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

const uint8_t Wifi_SERVICE_uuid[ESP_UUID_LEN_128] = {0x07, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WIFI_SERV_CHAR_descr[]  		= "ZAP Service";
static uint8_t WIFI_SERV_CHAR_val[32];
//static const uint8_t WIFI_SERV_descr[]              	= "ZAPTEC APM Service";


const uint8_t WifiSSID_uuid[ESP_UUID_LEN_128] = {0xd3, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t WIFI_SERV_CHAR_SSID_descr[]  		= "WiFi SSID";
static uint8_t WIFI_SERV_CHAR_SSID_val[32];        		//= "abcdefgh";//{0x00};

const uint8_t WiFiPSK_uid128[ESP_UUID_LEN_128] = {0xd4, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t WIFI_SERV_CHAR_PSK_descr[]   		= "Wifi password";
static uint8_t WIFI_SERV_CHAR_PSK_val[32]          		= {0x00};


//const uint8_t 	DeviceMID_uid128[ESP_UUID_LEN_128] = {0xc0, 0x6b, 0xe8, 0x8c, 0x3f, 0xe6, 0x1c, 0xe9, 0xfb, 0x4c, 0x22, 0xf7, 0x00, 0x00, 0x00, 0x00};
const uint8_t DeviceMID_uuid128[ESP_UUID_LEN_128] = {0xd7, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t ADAPTER_SERV_CHAR_ADAPTER_MID_descr[]  = "Device MID";
//static uint8_t ADAPTER_SERV_CHAR_ADAPTER_MID_val[9]    		= "APM000003";
static uint8_t ADAPTER_SERV_CHAR_ADAPTER_MID_val[9];


const uint8_t 	PIN_uuid128[ESP_UUID_LEN_128] = {0xd8, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t ADAPTER_SERV_CHAR_pin_descr[]  	= "Set PIN";
static uint8_t WIFI_SERV_CHAR_PIN_val[4]        		= {0x30, 0x30, 0x30, 0x30};


const uint8_t 	AvailableWifi_uuid128[ESP_UUID_LEN_128] = {0x01, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t AVAILABLE_WIFI_CHAR_pin_descr[]  	= "Available WiFi Networks";
//static uint8_t AVAILABLE_WIFI_SERV_CHAR_val[400];//      = {'a', 'c', 'e'};

const uint8_t 	NetworkStatus_uuid128[ESP_UUID_LEN_128] = {0x02, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t NETWORK_STATUS_CHAR_pin_descr[]  	= "Network Status";
//static uint8_t NETWORK_STATUS_SERV_CHAR_val[300];//      = {0x00, 0x00, 0x00, 0x00};

const uint8_t 	Warnings_uuid128[ESP_UUID_LEN_128] 		= {0x01, 0xfe, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
//static const uint8_t WARNINGS_CHAR_pin_descr[]  		= "Warnings";
//static uint8_t WARNINGS_SERV_CHAR_PIN_val[4]        	= {0x00, 0x00, 0x00, 0x00};




const uint8_t 	Auth_uuid128[ESP_UUID_LEN_128] 		= {0x00, 0xfd, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t AUTH_CHAR_pin_descr[]  		= "Auth";
static uint8_t AUTH_SERV_CHAR_val[]        		= {"0"};


const uint8_t 	Save_uuid128[ESP_UUID_LEN_128] 		= {0xd5, 0xfc, 0xb5, 0xc0, 0x50, 0x69, 0x5a, 0xa2, 0x77, 0x45, 0xec, 0xde, 0x5a, 0x2c, 0x49, 0x10};
static const uint8_t SAVE_CHAR_pin_descr[]  		= "Save";
static uint8_t SAVE_SERV_CHAR_val[]        		= {"0"};



static uint8_t WIFI_SERV_CHAR_info_ccc[2]           	= {0x00,0x00};
static uint8_t WIFI_SERV_CHAR_config_ccc[2]         	= {0x00,0x00};
static uint8_t ADAPTER_SERV_CHAR_config_ccc[2]      	= {0x11,0x22};

static char wifiPackage[500] = {0};







const esp_gatts_attr_db_t wifi_serv_gatt_db[WIFI_NB] =
{
	[WIFI_SERV_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_SERVICE_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uid128, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(WIFI_SERV_uuid), (uint8_t *)&WIFI_SERV_uuid}},
	//[WIFI_SERV] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uid128, ESP_GATT_PERM_READ, 0, 0, (uint8_t *)&WIFI_SERV_uuid}},

    //[WIFI_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Wifi_SERVICE_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	//[WIFI_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &WIFI_SERV_CHAR_descr, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},

	     [WIFI_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ,  sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},
	//[WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_info_ccc), (uint8_t *)WIFI_SERV_CHAR_info_ccc}},

	[WIFI_SSID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[WIFI_SSID_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &WifiSSID_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[WIFI_SSID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_info_ccc), (uint8_t *)WIFI_SERV_CHAR_info_ccc}},

	[WIFI_PSK_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
    [WIFI_PSK_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &WiFiPSK_uid128, ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
    [WIFI_PSK_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
    [WIFI_PSK_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(WIFI_SERV_CHAR_config_ccc), (uint8_t *)WIFI_SERV_CHAR_config_ccc}},

	[ADAPTER_DEVICE_MID_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_DEVICE_MID_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &DeviceMID_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_DEVICE_MID_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_DEVICE_MID_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

	[ADAPTER_PIN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_PIN_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &PIN_uuid128, ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_PIN_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_PIN_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},


	[ADAPTER_AVAIL_WIFI_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_AVAIL_WIFI_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &AvailableWifi_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_AVAIL_WIFI_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_AVAIL_WIFI_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

	[ADAPTER_NETWORK_STATUS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_NETWORK_STATUS_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	//[ADAPTER_NETWORK_STATUS_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) &NetworkStatus_uuid128, ESP_GATT_PERM_READ , sizeof(uint16_t), 0, NULL}},
	[ADAPTER_NETWORK_STATUS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_NETWORK_STATUS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

	[ADAPTER_WARNINGS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_WARNINGS_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Warnings_uuid128, ESP_GATT_PERM_READ, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_WARNINGS_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_WARNINGS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

	[ADAPTER_AUTH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_AUTH_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Auth_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_AUTH_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_AUTH_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

	[ADAPTER_SAVE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},
	[ADAPTER_SAVE_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t *) &Save_uuid128, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, NULL}},
	[ADAPTER_SAVE_DESCR] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &character_description, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, 0, NULL}},
	[ADAPTER_SAVE_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(ADAPTER_SERV_CHAR_config_ccc), (uint8_t *)ADAPTER_SERV_CHAR_config_ccc}},

};

void charInit()
{
#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
}


void setDeviceNameAsChar(char * devName)
{
	memcpy(ADAPTER_SERV_CHAR_ADAPTER_MID_val, devName, 9);
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


void handleWifiReadEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{
	//Check authentication before allowing reads
//	if((AUTH_SERV_CHAR_val[0] == 0) && (attrIndex != ADAPTER_DEVICE_MID_VAL))
//	{
//		ESP_LOGE(TAG, "Read: No pin set: %d", attrIndex);
//		return;
//	}

	char *jsonString;
	//char jsonString[] = "{\"wifi\":{\"ip\":\"10.0.0.1\",\"link\":-54},\"online\":true}";//"{'{', '"', 'o', 'n', 'l', 'i', 'n', 'e', '"', '=', 't', 'r', 'u', 'e', '}', '\0'};
	//char jsonString[] = "{\"online\":true}";
	//int jsonStringLen = strlen(jsonString);

	static int statusSegmentCount = 0;
	if(attrIndex != ADAPTER_NETWORK_STATUS_VAL)
		statusSegmentCount = 0;

	static int wifiSegmentCount = 0;
	if(attrIndex != ADAPTER_AVAIL_WIFI_VAL)
		wifiSegmentCount = 0;

	cJSON *jsonObject;
	cJSON *wifiObject;
	ESP_LOGE(TAG, "BLE index: %d", attrIndex);
    switch( attrIndex )
    {
    // Characteristic read values
    /*
     * Write only
     case WIFI_PSK_VAL:
	    memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
	    memcpy(rsp->attr_value.value, WIFI_SERV_CHAR_info_val, sizeof(WIFI_SERV_CHAR_info_val));
	    rsp->attr_value.len = sizeof(WIFI_SERV_CHAR_info_val);
	    break;*/

    case WIFI_SSID_VAL:
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


    case ADAPTER_DEVICE_MID_VAL:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, ADAPTER_SERV_CHAR_ADAPTER_MID_val, sizeof(ADAPTER_SERV_CHAR_ADAPTER_MID_val));
		rsp->attr_value.len = sizeof(ADAPTER_SERV_CHAR_ADAPTER_MID_val);
		break;

    case ADAPTER_DEVICE_MID_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, ADAPTER_SERV_CHAR_ADAPTER_MID_descr, sizeof(ADAPTER_SERV_CHAR_ADAPTER_MID_descr));
		rsp->attr_value.len = sizeof(ADAPTER_SERV_CHAR_ADAPTER_MID_descr);
		break;

    case ADAPTER_PIN_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, ADAPTER_SERV_CHAR_pin_descr, sizeof(ADAPTER_SERV_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(ADAPTER_SERV_CHAR_pin_descr);
		break;

    case ADAPTER_AUTH_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, AUTH_CHAR_pin_descr, sizeof(AUTH_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(AUTH_CHAR_pin_descr);
		break;
    case ADAPTER_SAVE_DESCR:
		memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, SAVE_CHAR_pin_descr, sizeof(SAVE_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(SAVE_CHAR_pin_descr);
		break;

    case ADAPTER_AVAIL_WIFI_DESCR:
    	memset(rsp->attr_value.value, 0, sizeof(rsp->attr_value.value));
		memcpy(rsp->attr_value.value, AVAILABLE_WIFI_CHAR_pin_descr, sizeof(AVAILABLE_WIFI_CHAR_pin_descr));
		rsp->attr_value.len = sizeof(AVAILABLE_WIFI_CHAR_pin_descr);
		break;

    case ADAPTER_AVAIL_WIFI_VAL:

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

    case ADAPTER_NETWORK_STATUS_VAL:

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

    case ADAPTER_AUTH_VAL:
    	rsp->attr_value.value[0] = AUTH_SERV_CHAR_val[0];
    	rsp->attr_value.len = 1;
    	break;

    case ADAPTER_SAVE_VAL:
    	rsp->attr_value.value[0] = SAVE_SERV_CHAR_val[0];
    	rsp->attr_value.len = 1;
        break;

    }

}

void handleWifiWriteEvent(int attrIndex, esp_ble_gatts_cb_param_t* param, esp_gatt_rsp_t* rsp)
{
	//Check authentication before allowing writes
//	if((AUTH_SERV_CHAR_val[0] == 0) && (attrIndex != ADAPTER_AUTH_VAL))
//	{
//		ESP_LOGE(TAG, "Write: No pin set: %d", attrIndex);
//		return;
//	}

    switch( attrIndex )
    {
    case WIFI_PSK_VAL:
        /*
         *  Handle any writes to Wifi Info char here
         */

        // This prints the first byte written to the characteristic
        ESP_LOGI(TAG, "Wifi Info characteristic written with %02x", param->write.value[0]);

        memset(WIFI_SERV_CHAR_PSK_val,0, sizeof(WIFI_SERV_CHAR_PSK_val));
		memcpy(WIFI_SERV_CHAR_PSK_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Wifi SSID %s", WIFI_SERV_CHAR_PSK_val);

//		storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);
//
//		//Make the values
//		network_CheckWifiParameters();
	    break;

    case WIFI_SSID_VAL:
        /*
         *  Handle any writes to Wifi Val char here
         */
    	ESP_LOGI(TAG, "Wifi Val characteristic written with %02x", param->write.value[0]);

    	memset(WIFI_SERV_CHAR_SSID_val,0, sizeof(WIFI_SERV_CHAR_SSID_val));
		memcpy(WIFI_SERV_CHAR_SSID_val,param->write.value, param->write.len);
		ESP_LOGI(TAG, "New Wifi SSID %s", WIFI_SERV_CHAR_SSID_val);


	    break;

    case ADAPTER_AUTH_VAL:

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

    case ADAPTER_SAVE_VAL:
		//ESP_LOGI(TAG, "Save value %02x", param->write.value[0]);

    	///wasValid = network_wifiIsValid();

		SAVE_SERV_CHAR_val[0] = '1';
		if(SAVE_SERV_CHAR_val[0] == '1')
		{
			///storage_SaveWifiParameters((char*)WIFI_SERV_CHAR_SSID_val, (char*)WIFI_SERV_CHAR_PSK_val);

			//Make the values active
			///network_CheckWifiParameters();
		}

		///if(wasValid == true)
			///network_updateWifi();

		ESP_LOGI(TAG, "Save val pin %s", SAVE_SERV_CHAR_val);

		break;
	}


}

void ClearAuthValue()
{
	AUTH_SERV_CHAR_val[0] = 0;
	SAVE_SERV_CHAR_val[0] = 0;
}
