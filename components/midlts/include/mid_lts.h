#ifndef __MID_LTS_H__
#define __MID_LTS_H__

#if defined(__aarch64__) || defined(__x86_64__)
#define HOST
#endif

#ifdef HOST

#include <stdlib.h>
#include <time.h>

__attribute__((unused)) static uint32_t esp_crc32_le(uint32_t crc, uint8_t *buf, size_t len) {
   crc = ~crc;
   while (len--) {
      crc ^= *buf++;
      for (int k = 0; k < 8; k++) {
         crc = crc & 1 ? (crc >> 1) ^ 0x82F63B78 : crc >> 1;
      }
   }
   return ~crc;
}

__attribute__((unused)) static uint32_t esp_random(void) {
	return (uint32_t)arc4random();
}

#define MIDLTS_YIELD

#define ESP_LOGE(tag, fmt, ...) printf("%s:E: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("%s:I: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("%s:D: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) printf("%s:V: " fmt "\n", tag, ## __VA_ARGS__)

#define MIDLTS_DIR "./"

#else

#include "esp_crc.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MIDLTS_YIELD taskYIELD()

#define MIDLTS_DIR "/mid/"


#endif

#include "mid_lts_priv.h"
#include "mid_session.pb.h"

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

midlts_err_t mid_session_get_record(midlts_pos_t *pos, mid_session_record_t *out);

#endif
