#ifndef __MID_SESSION_H__
#define __MID_SESSION_H__

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

typedef enum {
	MS_FLAG_NONE            = 0x0,
	MS_FLAG_STOPPED_BY_RFID = 0x1,
	MS_FLAG_PUBLISHED       = 0x2, // Mark as sent to cloud (for CompletedSession)
	MS_FLAG_COMPLETE        = 0x4,
} midsess_flag_t;

#define MID_SESS_SIZE_ID 37
#define MID_SESS_SIZE_AUTH 41

typedef struct {
	uint64_t time_sec;
	uint32_t time_usec;
} midsess_time_t;

typedef struct {
	uint8_t v1;
	uint8_t v2;
	uint8_t v3;
} midsess_ver_mid_t;

typedef struct {
	uint16_t v1 : 10; // Up to 1023.1023.1023.1023 should be good?
	uint16_t v2 : 10;
	uint16_t v3 : 10;
	uint16_t v4 : 10;
} midsess_ver_app_t;

typedef enum {
	MV_FLAG_NONE              =   0x0,
	MV_FLAG_READING_START     =   0x1,
	MV_FLAG_READING_TARIFF    =   0x2,
	MV_FLAG_READING_END       =   0x4,
	MV_FLAG_TIME_UNKNOWN      =   0x8,
	MV_FLAG_TIME_INFORMATIVE  =  0x10,
	MV_FLAG_TIME_SYNCHRONIZED =  0x20,
	MV_FLAG_TIME_RELATIVE     =  0x40,
	MV_FLAG_METER_ERROR       =  0x80,
	MV_FLAG_PUBLISHED         = 0x100, // Mark as sent to cloud (for SignedMeterValue)
} midsess_meter_flag_t;

typedef struct {
	uint64_t meter_time;
	uint32_t meter_value;
	// Versions are stored alongside meter value so we can store tariff changes
	midsess_ver_app_t meter_vapp;
	midsess_ver_mid_t meter_vmid;
	uint32_t meter_flag;
} midsess_meter_val_t;

typedef struct {
	// CRC is over whole session package, including meter values
	uint32_t sess_crc;
	char sess_id[MID_SESS_SIZE_ID];
	char sess_auth[MID_SESS_SIZE_AUTH];
	midsess_time_t sess_time_start;
	midsess_time_t sess_time_end;
	uint32_t sess_flag;
	uint32_t sess_count;
	midsess_meter_val_t sess_values[0];
} midsess_t;

_Static_assert(offsetof(midsess_t, sess_values[0]) == sizeof (midsess_t), "Zero-sized array must be final element!");

#endif
