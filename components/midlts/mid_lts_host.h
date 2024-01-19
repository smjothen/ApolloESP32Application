#ifndef __MID_LTS_HOST_H__
#define __MID_LTS_HOST_H__

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>

typedef struct {
   bool init;
   uint8_t *flash;
} esp_partition_t;

typedef enum {
   ESP_OK,
   ESP_ERR
} esp_err_t;

static esp_partition_t part = {.init = false, .flash = NULL};
static const esp_partition_t *partition = &part;

#define ESP_PARTITION_TYPE_ANY 0
#define ESP_PARTITION_SUBTYPE_ANY 1

static inline const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
   (void)type;
   (void)subtype;
   (void)label;
   return partition;
}

static inline void esp_partition_init(esp_partition_t *partition) {
   if (!partition->init) {
      int fd = open("/tmp/flash", O_RDONLY);
      if (fd < 0) {
         FILE *fp = fopen("/tmp/flash", "w");
         size_t todo = FLASH_TOTAL_SIZE;
         while (todo > 0) {
            uint8_t c = 0xff;
            fwrite(&c, 1, sizeof (c), fp);
            todo--;
         }
         fclose(fp);
      }
      fd = open("/tmp/flash", O_RDWR);
      partition->init = true;
      partition->flash = mmap(0, FLASH_TOTAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      assert(partition->flash != MAP_FAILED);
   }
}

static inline esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size) {
   assert(partition);
   assert(src_offset + size <= FLASH_TOTAL_SIZE);
   assert(size % 16 == 0);
   esp_partition_init((esp_partition_t *)partition);
   for (size_t i = src_offset; i < src_offset + size; i++) {
      ((uint8_t *)dst)[i - src_offset] = partition->flash[i];
   }
   return ESP_OK;
}

static inline esp_err_t esp_partition_read_raw(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size) {
   return esp_partition_read(partition, src_offset, dst, size);
}

static inline esp_err_t esp_partition_write(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size) {
   assert(partition);
   assert(dst_offset % 16 == 0);
   assert(size % 16 == 0);
   assert(dst_offset + size <= FLASH_TOTAL_SIZE);
   esp_partition_init((esp_partition_t *)partition);
   for (size_t i = dst_offset; i < dst_offset + size; i++) {
      ((esp_partition_t *)partition)->flash[i] = ((uint8_t *)src)[i - dst_offset];
   }
   return ESP_OK;
}

static inline esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
   assert(partition);
   assert(offset % 16 == 0);
   assert(size % 16 == 0);
   assert(offset + size <= FLASH_TOTAL_SIZE);
   esp_partition_init((esp_partition_t *)partition);
   memset((void*)(partition->flash + offset), 0xff, size);
   return ESP_OK;
}

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
