#ifndef __MID_LTS_H__
#define __MID_LTS_H__

#if defined(__aarch64__) || defined(__x86_64__)
#define HOST
#endif

#ifdef HOST

#include <time.h>

static uint32_t esp_crc32_le(uint32_t crc, uint8_t *buf, size_t len) {
   crc = ~crc;
   while (len--) {
      crc ^= *buf++;
      for (int k = 0; k < 8; k++) {
         crc = crc & 1 ? (crc >> 1) ^ 0x82F63B78 : crc >> 1;
      }
   }
   return ~crc;
}

static uint32_t esp_random(void) {
	return (uint32_t)arc4random();
}

#define ESP_LOGE(tag, fmt, ...) printf("E: " fmt "\n", ## __VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I: " fmt "\n", ## __VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("D: " fmt "\n", ## __VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) printf("V: " fmt "\n", ## __VA_ARGS__)

#define MIDLTS_DIR "./"

#else

#include "esp_crc.h"
#include "esp_random.h"
#include "esp_log.h"

#define MIDLTS_DIR "/files/"

#endif

#include "mid_session.h"

typedef struct _midlts_ctx_t midlts_ctx_t;
typedef enum _midlts_err_t midlts_err_t;

midlts_err_t mid_session_init(midlts_ctx_t *ctx, midsess_ver_app_t app);
midlts_err_t mid_session_set_id(midlts_ctx_t *ctx, const char *id);
midlts_err_t mid_session_set_auth(midlts_ctx_t *ctx, const char *auth);
midlts_err_t mid_session_set_start_time(midlts_ctx_t *ctx, uint64_t tm_sec, uint32_t tm_usec);
midlts_err_t mid_session_set_end_time(midlts_ctx_t *ctx, uint64_t tm_sec, uint32_t tm_usec);
midlts_err_t mid_session_set_flag(midlts_ctx_t *ctx, midsess_flag_t flag);

#endif
