#ifndef __MID_LTS_PRIV_H__
#define __MID_LTS_PRIV_H__

#include "mid_session.h"

#define MIDLTS_LOG_MAX_AGE ((uint64_t)(31 * 24 * 60 * 60))
#define MIDLTS_LOG_MAX_SIZE 4096

// This should be set to around 3/4 of the partition size to account
// for metadata storage, etc of LittleFS!
//
// This is 3/4 of 0xc0000 = 0x90000 = 144 files
#define MIDLTS_LOG_MAX_FILES 144

#define MIDLTS_SCN "%" SCNx32 ".ms%n"
#define MIDLTS_PRI "%" PRIx32 ".ms"

typedef uint32_t midlts_id_t;

typedef enum _midlts_flag_t {
	LTS_FLAG_NONE = 0,
	LTS_FLAG_SESSION_OPEN = 1,
	LTS_FLAG_REPLAY_PRINT = 2,
} midlts_flag_t;

typedef struct _midlts_pos_t {
	// Log id
	uint16_t id;
	// Offset in log
	uint16_t offset;
	// CRC of record
	uint32_t crc;
} midlts_pos_t;

#define MIDLTS_POS_MAX ((midlts_pos_t){ .loc = 0xFFFFFFFF, .crc = 0xFFFFFFFF })

typedef struct _midlts_ctx_t {
	mid_session_version_fw_t fw_version;
	mid_session_version_lr_t lr_version;

	uint32_t flags;
	size_t max_pages;

	midlts_id_t msg_page;
	midlts_id_t msg_id;

	// Minimum id of stored item in auxiliary storage (offline session/log), anything prior
	// to this can be purged (if older than the max age as well)
	midlts_pos_t min_purgeable;
} midlts_ctx_t;

#define MID_SESSION_IS_OPEN(ctx) (!!((ctx)->flags & LTS_FLAG_SESSION_OPEN))
#define MID_SESSION_IS_CLOSED(ctx) (!MID_SESSION_IS_OPEN(ctx))

#define MIDLTS_ERROR_LIST \
	X(LTS_OK) \
	X(LTS_ERASE) \
	X(LTS_WRITE) \
	X(LTS_READ) \
	X(LTS_FLUSH) \
	X(LTS_TELL) \
	X(LTS_SEEK) \
	X(LTS_SYNC) \
	X(LTS_STAT) \
	X(LTS_CLOSE) \
	X(LTS_OPEN) \
	X(LTS_BAD_ARG) \
	X(LTS_BAD_CRC) \
	X(LTS_MSG_OUT_OF_ORDER) \
	X(LTS_LOG_FILE_FULL) \
	X(LTS_SESSION_NOT_OPEN) \
	X(LTS_SESSION_ALREADY_OPEN) \

#define X(e) e,
typedef enum _midlts_err_t {
	MIDLTS_ERROR_LIST
} midlts_err_t;
#undef X

#define X(e) #e,
static const char *_midlts_error_list[] = {
	MIDLTS_ERROR_LIST
};
#undef X

static inline const char *mid_session_err_to_string(midlts_err_t err) {
	return (err < sizeof (_midlts_error_list) / sizeof (_midlts_error_list[0]) ? _midlts_error_list[err] : "LTS_UNKNOWN");
}

const char *mid_session_get_auth_type_name(mid_session_auth_type_t type);
const char *mid_session_get_type_name(mid_session_record_t *rec);

void mid_session_format_bytes_uuid(char *buf, uint8_t *bytes, size_t len);
void mid_session_format_bytes(char *buf, uint8_t *bytes, size_t len);

void mid_session_print_record(mid_session_record_t *rec);

#endif
