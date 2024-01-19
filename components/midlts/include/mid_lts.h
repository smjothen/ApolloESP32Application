#ifndef __MID_LTS_H__
#define __MID_LTS_H__

#define FLASH_PAGE_SIZE 4096

#if defined(__aarch64__) || defined(__x86_64__)
#define HOST
#include "mid_lts_host.h"
#else
#include <string.h>
#include "esp_crc.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_partition.h"
#endif

#include "mid_lts_priv.h"
#include "mid_session.h"

typedef struct _midlts_ctx_t midlts_ctx_t;
typedef struct _midlts_pos_t midlts_pos_t;
typedef enum _midlts_err_t midlts_err_t;

midlts_err_t mid_session_init(midlts_ctx_t *ctx, time_t now, const char *fw_version, const char *lr_version);
midlts_err_t mid_session_set_purge_limit(midlts_ctx_t *ctx, midlts_pos_t *pos);

midlts_err_t mid_session_add_open(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16], mid_session_meter_value_flag_t flag, uint32_t meter);
midlts_err_t mid_session_add_tariff(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter);
midlts_err_t mid_session_add_close(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_meter_value_flag_t flag, uint32_t meter);
midlts_err_t mid_session_add_id(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, uint8_t uuid[16]);
midlts_err_t mid_session_add_auth(midlts_ctx_t *ctx, midlts_pos_t *pos, time_t now, mid_session_auth_type_t type, uint8_t *data, size_t data_size);

midlts_err_t mid_session_read_record(midlts_ctx_t *ctx, midlts_pos_t *pos, mid_session_record_t *rec);

#endif
