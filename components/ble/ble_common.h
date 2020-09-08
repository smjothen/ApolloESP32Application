
#ifndef BLE_COMMON_H_
    #define BLE_COMMON_H_

    //#include "/Users/eirik/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_gatts_api.h"
	//#include "../../../components/bt/host/bluedroid/api/include/api/esp_gatts_api.h"
	#include "esp_gatts_api.h"

    #define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))

    extern const uint16_t primary_service_uuid;
    extern const uint16_t character_client_config_uuid;
    extern const uint16_t character_declaration_uuid;
    extern const uint16_t character_description; //0x2901

    extern const uint8_t char_prop_notify;
    extern const uint8_t char_prop_read;
    extern const uint8_t char_prop_read_notify;
    extern const uint8_t char_prop_read_write;
    extern const uint8_t char_prop_read_write_notify;
    extern const uint8_t char_prop_write;

#endif /* BLE_COMMON_H_ */
