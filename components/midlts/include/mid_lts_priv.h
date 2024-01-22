#ifndef __MID_LTS_PRIV_H__
#define __MID_LTS_PRIV_H__

#include "mid_session.h"

#define MIDLTS_LOG_MAX_AGE ((uint64_t)(31 * 24 * 60 * 60))

typedef uint32_t midlts_id_t;
typedef uint32_t midlts_msg_id_t;

typedef enum _midlts_flag_t {
	LTS_FLAG_NONE = 0,
	LTS_FLAG_SESSION_OPEN = 1,
	LTS_FLAG_REPLAY_PRINT = 2,
} midlts_flag_t;

typedef struct _midlts_pos_t {
	// Offset in flash
	uint32_t loc;
	// CRC of original record
	uint32_t id;
} midlts_pos_t;

#define MIDLTS_POS_MAX ((midlts_pos_t){ .loc = 0xFFFFFFFF, .crc = 0xFFFFFFFF })

#define MIDLTS_PAGE_EMPTY 0
#define MIDLTS_PAGE_VALID 1
#define MIDLTS_PAGE_INVALID 2

#define MIDLTS_PAGE_MAX 128

typedef struct _midlts_ctx_t {
	const esp_partition_t *partition;

	mid_session_version_fw_t fw_version;
	mid_session_version_lr_t lr_version;

	uint32_t flags;
	uint8_t status[MIDLTS_PAGE_MAX];

	midlts_msg_id_t msg_addr;
	midlts_msg_id_t msg_id;

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
	X(LTS_BAD_ARG) \
	X(LTS_BAD_CRC) \
	X(LTS_CORRUPT) \
	X(LTS_PARSE_VERSION) \
	X(LTS_MSG_OUT_OF_ORDER) \
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
