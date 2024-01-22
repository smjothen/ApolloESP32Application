#ifndef __MID_SESSION_H__
#define __MID_SESSION_H__

#include <stdint.h>

#define PACK __attribute__((packed))

typedef enum {
    MID_SESSION_AUTH_TYPE_CLOUD = 0,
    MID_SESSION_AUTH_TYPE_RFID = 1,
    MID_SESSION_AUTH_TYPE_BLE = 2,
    MID_SESSION_AUTH_TYPE_ISO15118 = 3,
    MID_SESSION_AUTH_TYPE_NEXTGEN = 4
} mid_session_auth_type_t;

/* Struct definitions */
typedef struct {
    uint8_t uuid[16];
} PACK mid_session_id_t;

typedef struct {
    mid_session_auth_type_t type : 8;
    uint8_t length;
    uint8_t tag[21];
} PACK mid_session_auth_t;

typedef enum {
    MID_SESSION_METER_VALUE_READING_FLAG_START = 1,
    MID_SESSION_METER_VALUE_READING_FLAG_TARIFF = 2,
    MID_SESSION_METER_VALUE_READING_FLAG_END = 4,
} mid_session_meter_value_reading_flag_t;

typedef enum {
    MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN = 8,
    MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE = 16,
    MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED = 32,
    MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE = 64,
    MID_SESSION_METER_VALUE_FLAG_METER_ERROR = 128,
} mid_session_meter_value_flag_t;

typedef struct {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
} PACK mid_session_version_lr_t;

// Up to 1023.1023.1023.1023 should be enough?
typedef struct {
	uint16_t major : 10;
	uint16_t minor : 10;
	uint16_t patch : 10;
	uint16_t extra : 10;
} PACK mid_session_version_fw_t;

// Set recent epoch so we can save 4 bytes:
//
// Jan 1 2020 00:00 GMT
#define MID_EPOCH 1577836800
#define MID_TIME_PACK(time) ((time) < MID_EPOCH ? 0 : ((time) - MID_EPOCH))
#define MID_TIME_UNPACK(time) ((time_t)((time) + MID_EPOCH))

typedef struct {
	mid_session_version_lr_t lr;
	mid_session_version_fw_t fw;
    uint32_t time;
    uint32_t flag;
    uint32_t meter;
} PACK mid_session_meter_value_t;

typedef enum {
    MID_SESSION_RECORD_TYPE_ID = 0,
    MID_SESSION_RECORD_TYPE_AUTH = 1,
    MID_SESSION_RECORD_TYPE_METER_VALUE = 2,
} mid_session_record_type_t;

typedef struct {
    mid_session_record_type_t rec_type : 8;
    uint32_t rec_id;
    uint32_t rec_crc;
    union {
        mid_session_id_t id;
        mid_session_auth_t auth;
        mid_session_meter_value_t meter_value;
    };
} PACK mid_session_record_t;

_Static_assert(sizeof (mid_session_record_t) == 32, "Size of record must remain 32 bytes!");

#endif
