/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.7 */

#ifndef PB_MID_SESSION_PB_H_INCLUDED
#define PB_MID_SESSION_PB_H_INCLUDED
#include "pb.h"

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Enum definitions */
typedef enum mid_session_meter_value_reading_flag {
    MID_SESSION_METER_VALUE_READING_FLAG_START = 1,
    MID_SESSION_METER_VALUE_READING_FLAG_TARIFF = 2,
    MID_SESSION_METER_VALUE_READING_FLAG_END = 4
} mid_session_meter_value_reading_flag_t;

typedef enum mid_session_meter_value_flag {
    MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN = 8,
    MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE = 16,
    MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED = 32,
    MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE = 64,
    MID_SESSION_METER_VALUE_FLAG_METER_ERROR = 128
} mid_session_meter_value_flag_t;

typedef enum mid_session_auth_type {
    MID_SESSION_AUTH_TYPE_CLOUD = 0,
    MID_SESSION_AUTH_TYPE_RFID = 1,
    MID_SESSION_AUTH_TYPE_BLE = 2,
    MID_SESSION_AUTH_TYPE_ISO15118 = 3,
    MID_SESSION_AUTH_TYPE_NEXTGEN = 4
} mid_session_auth_type_t;

/* Struct definitions */
PB_PACKED_STRUCT_START
typedef struct mid_session_id {
    pb_byte_t uuid[16];
} pb_packed mid_session_id_t;
PB_PACKED_STRUCT_END

typedef PB_BYTES_ARRAY_T(32) mid_session_auth_tag_t;
PB_PACKED_STRUCT_START
typedef struct mid_session_auth {
    mid_session_auth_type_t type;
    mid_session_auth_tag_t tag;
} pb_packed mid_session_auth_t;
PB_PACKED_STRUCT_END

PB_PACKED_STRUCT_START
typedef struct mid_session_version {
    char code[32];
} pb_packed mid_session_version_t;
PB_PACKED_STRUCT_END

PB_PACKED_STRUCT_START
typedef struct mid_session_meter_value {
    uint64_t time;
    mid_session_meter_value_flag_t flag;
    uint32_t meter;
} pb_packed mid_session_meter_value_t;
PB_PACKED_STRUCT_END

PB_PACKED_STRUCT_START
typedef struct mid_session_record {
    uint32_t rec_id;
    uint32_t rec_crc;
    bool has_id;
    mid_session_id_t id;
    bool has_auth;
    mid_session_auth_t auth;
    bool has_lr_version;
    mid_session_version_t lr_version;
    bool has_meter_value;
    mid_session_meter_value_t meter_value;
    bool has_fw_version;
    mid_session_version_t fw_version;
} pb_packed mid_session_record_t;
PB_PACKED_STRUCT_END


#ifdef __cplusplus
extern "C" {
#endif

/* Helper constants for enums */
#define _MID_SESSION_METER_VALUE_READING_FLAG_MIN MID_SESSION_METER_VALUE_READING_FLAG_START
#define _MID_SESSION_METER_VALUE_READING_FLAG_MAX MID_SESSION_METER_VALUE_READING_FLAG_END
#define _MID_SESSION_METER_VALUE_READING_FLAG_ARRAYSIZE ((mid_session_meter_value_reading_flag_t)(MID_SESSION_METER_VALUE_READING_FLAG_END+1))

#define _MID_SESSION_METER_VALUE_FLAG_MIN MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN
#define _MID_SESSION_METER_VALUE_FLAG_MAX MID_SESSION_METER_VALUE_FLAG_METER_ERROR
#define _MID_SESSION_METER_VALUE_FLAG_ARRAYSIZE ((mid_session_meter_value_flag_t)(MID_SESSION_METER_VALUE_FLAG_METER_ERROR+1))

#define _MID_SESSION_AUTH_TYPE_MIN MID_SESSION_AUTH_TYPE_CLOUD
#define _MID_SESSION_AUTH_TYPE_MAX MID_SESSION_AUTH_TYPE_NEXTGEN
#define _MID_SESSION_AUTH_TYPE_ARRAYSIZE ((mid_session_auth_type_t)(MID_SESSION_AUTH_TYPE_NEXTGEN+1))


#define mid_session_auth_t_type_ENUMTYPE mid_session_auth_type_t


#define mid_session_meter_value_t_flag_ENUMTYPE mid_session_meter_value_flag_t



/* Initializer values for message structs */
#define MID_SESSION_ID_INIT_DEFAULT              {{0}}
#define MID_SESSION_AUTH_INIT_DEFAULT            {_MID_SESSION_AUTH_TYPE_MIN, {0, {0}}}
#define MID_SESSION_VERSION_INIT_DEFAULT         {""}
#define MID_SESSION_METER_VALUE_INIT_DEFAULT     {0, _MID_SESSION_METER_VALUE_FLAG_MIN, 0}
#define MID_SESSION_RECORD_INIT_DEFAULT          {0, 0, false, MID_SESSION_ID_INIT_DEFAULT, false, MID_SESSION_AUTH_INIT_DEFAULT, false, MID_SESSION_VERSION_INIT_DEFAULT, false, MID_SESSION_METER_VALUE_INIT_DEFAULT, false, MID_SESSION_VERSION_INIT_DEFAULT}
#define MID_SESSION_ID_INIT_ZERO                 {{0}}
#define MID_SESSION_AUTH_INIT_ZERO               {_MID_SESSION_AUTH_TYPE_MIN, {0, {0}}}
#define MID_SESSION_VERSION_INIT_ZERO            {""}
#define MID_SESSION_METER_VALUE_INIT_ZERO        {0, _MID_SESSION_METER_VALUE_FLAG_MIN, 0}
#define MID_SESSION_RECORD_INIT_ZERO             {0, 0, false, MID_SESSION_ID_INIT_ZERO, false, MID_SESSION_AUTH_INIT_ZERO, false, MID_SESSION_VERSION_INIT_ZERO, false, MID_SESSION_METER_VALUE_INIT_ZERO, false, MID_SESSION_VERSION_INIT_ZERO}

/* Field tags (for use in manual encoding/decoding) */
#define MID_SESSION_ID_UUID_TAG                  1
#define MID_SESSION_AUTH_TYPE_TAG                1
#define MID_SESSION_AUTH_TAG_TAG                 2
#define MID_SESSION_VERSION_CODE_TAG             2
#define MID_SESSION_METER_VALUE_TIME_TAG         1
#define MID_SESSION_METER_VALUE_FLAG_TAG         2
#define MID_SESSION_METER_VALUE_METER_TAG        3
#define MID_SESSION_RECORD_REC_ID_TAG            1
#define MID_SESSION_RECORD_REC_CRC_TAG           2
#define MID_SESSION_RECORD_ID_TAG                3
#define MID_SESSION_RECORD_AUTH_TAG              4
#define MID_SESSION_RECORD_LR_VERSION_TAG        5
#define MID_SESSION_RECORD_METER_VALUE_TAG       6
#define MID_SESSION_RECORD_FW_VERSION_TAG        7

/* Struct field encoding specification for nanopb */
#define MID_SESSION_ID_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, FIXED_LENGTH_BYTES, uuid,              1)
#define MID_SESSION_ID_CALLBACK NULL
#define MID_SESSION_ID_DEFAULT NULL

#define MID_SESSION_AUTH_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UENUM,    type,              1) \
X(a, STATIC,   REQUIRED, BYTES,    tag,               2)
#define MID_SESSION_AUTH_CALLBACK NULL
#define MID_SESSION_AUTH_DEFAULT NULL

#define MID_SESSION_VERSION_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, STRING,   code,              2)
#define MID_SESSION_VERSION_CALLBACK NULL
#define MID_SESSION_VERSION_DEFAULT NULL

#define MID_SESSION_METER_VALUE_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT64,   time,              1) \
X(a, STATIC,   REQUIRED, UENUM,    flag,              2) \
X(a, STATIC,   REQUIRED, UINT32,   meter,             3)
#define MID_SESSION_METER_VALUE_CALLBACK NULL
#define MID_SESSION_METER_VALUE_DEFAULT (const pb_byte_t*)"\x10\x08\x00"

#define MID_SESSION_RECORD_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   rec_id,            1) \
X(a, STATIC,   REQUIRED, UINT32,   rec_crc,           2) \
X(a, STATIC,   OPTIONAL, MESSAGE,  id,                3) \
X(a, STATIC,   OPTIONAL, MESSAGE,  auth,              4) \
X(a, STATIC,   OPTIONAL, MESSAGE,  lr_version,        5) \
X(a, STATIC,   OPTIONAL, MESSAGE,  meter_value,       6) \
X(a, STATIC,   OPTIONAL, MESSAGE,  fw_version,        7)
#define MID_SESSION_RECORD_CALLBACK NULL
#define MID_SESSION_RECORD_DEFAULT NULL
#define mid_session_record_t_id_MSGTYPE mid_session_id_t
#define mid_session_record_t_auth_MSGTYPE mid_session_auth_t
#define mid_session_record_t_lr_version_MSGTYPE mid_session_version_t
#define mid_session_record_t_meter_value_MSGTYPE mid_session_meter_value_t
#define mid_session_record_t_fw_version_MSGTYPE mid_session_version_t

extern const pb_msgdesc_t mid_session_id_t_msg;
extern const pb_msgdesc_t mid_session_auth_t_msg;
extern const pb_msgdesc_t mid_session_version_t_msg;
extern const pb_msgdesc_t mid_session_meter_value_t_msg;
extern const pb_msgdesc_t mid_session_record_t_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define MID_SESSION_ID_FIELDS &mid_session_id_t_msg
#define MID_SESSION_AUTH_FIELDS &mid_session_auth_t_msg
#define MID_SESSION_VERSION_FIELDS &mid_session_version_t_msg
#define MID_SESSION_METER_VALUE_FIELDS &mid_session_meter_value_t_msg
#define MID_SESSION_RECORD_FIELDS &mid_session_record_t_msg

/* Maximum encoded size of messages (where known) */
#define MID_SESSION_AUTH_SIZE                    36
#define MID_SESSION_ID_SIZE                      18
#define MID_SESSION_METER_VALUE_SIZE             20
#define MID_SESSION_RECORD_SIZE                  162
#define MID_SESSION_VERSION_SIZE                 33

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
