#ifndef __MID_LTS_H__
#define __MID_LTS_H__

#if defined(__aarch64__) || defined(__x86_64__)
#define HOST
#include "mid_lts_host.h"
#else
#include <string.h>
#include "esp_crc.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_partition.h"

#define MIDLTS_DIR "/mid/"
#endif

#include <time.h>
#include "mid_lts_priv.h"
#include "mid_session.h"

typedef struct _midlts_ctx_t midlts_ctx_t;
typedef union _midlts_pos_t midlts_pos_t;
typedef enum _midlts_err_t midlts_err_t;

midlts_err_t mid_session_init(midlts_ctx_t *ctx, mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version);
void mid_session_free(midlts_ctx_t *ctx);

midlts_err_t mid_session_set_lts_purge_limit(midlts_ctx_t *ctx, midlts_pos_t *pos);
midlts_err_t mid_session_run_purge(midlts_ctx_t *ctx, const struct timespec now);

// Open, close or add tariff change
midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, const struct timespec now, mid_session_meter_value_flag_t flag, uint32_t meter);
midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, const struct timespec now, mid_session_meter_value_flag_t flag, uint32_t meter);
midlts_err_t mid_session_add_close(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, const struct timespec now, mid_session_meter_value_flag_t flag, uint32_t meter);

// Add metadata to an open session
midlts_err_t mid_session_add_id(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, const struct timespec now, uint8_t uuid[16]);
midlts_err_t mid_session_add_auth(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *out, const struct timespec now, mid_session_auth_source_t source, mid_session_auth_type_t type, uint8_t *data, size_t data_size);

midlts_err_t mid_session_read_record(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec);
midlts_err_t mid_session_read_session(midlts_ctx_t *ctx, midlts_pos_t *pos);

#endif
