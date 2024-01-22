#ifndef __MID_LTS_HOST_H__
#define __MID_LTS_HOST_H__

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>

static inline uint32_t esp_crc32_le(uint32_t crc, uint8_t *buf, size_t len) {
   crc = ~crc;
   while (len--) {
      crc ^= *buf++;
      for (int k = 0; k < 8; k++) {
         crc = crc & 1 ? (crc >> 1) ^ 0x82F63B78 : crc >> 1;
      }
   }
   return ~crc;
}

static inline uint32_t esp_random(void) {
	return (uint32_t)arc4random();
}

#define ESP_LOGE(tag, fmt, ...) printf("%s:E: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("%s:I: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("%s:D: " fmt "\n", tag, ## __VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) printf("%s:V: " fmt "\n", tag, ## __VA_ARGS__)

#define MIDLTS_DIR "./"

#endif
