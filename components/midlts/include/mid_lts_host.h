#ifndef __MID_LTS_HOST_H__
#define __MID_LTS_HOST_H__

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>

#define ESP_PARTITION_TYPE_ANY 0
#define ESP_PARTITION_SUBTYPE_ANY 1

typedef struct {
	bool init;
	size_t size;
	uint8_t *flash;
	uint8_t *erase_bit;
} esp_partition_t;

typedef enum {
   ESP_OK,
   ESP_ERR
} esp_err_t;

static inline const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
	(void)type;
	(void)subtype;
	(void)label;
	static esp_partition_t part = {.size = FLASH_PAGE_SIZE * 4, .init = false, .flash = NULL, .erase_bit = NULL};
	return &part;
}

static inline uint8_t *mmap_file(esp_partition_t *partition, const char *name, uint8_t fill) {
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		FILE *fp = fopen(name, "w");
		assert(fp);
		size_t todo = partition->size;
		while (todo-- > 0) {
			size_t w = fwrite(&fill, 1, sizeof (fill), fp);
			assert(w == 1);
		}
		fclose(fp);
	}
	fd = open(name, O_RDWR);
	assert(fd >= 0);
	uint8_t *mmap0 = mmap(0, partition->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	assert(mmap0 != MAP_FAILED);
	return mmap0;
}

static inline void esp_partition_init(esp_partition_t *partition) {
   if (!partition->init) {
	   partition->flash = mmap_file(partition, "/tmp/flash", 0xff);
	   assert(partition->flash);
	   partition->erase_bit = mmap_file(partition, "/tmp/erase", 0x0);
	   assert(partition->erase_bit);
	   partition->init = 1;
   }
}

static inline esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size) {
   assert(partition);
   assert(src_offset + size <= partition->size);
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
   assert(dst_offset + size <= partition->size);
   esp_partition_init((esp_partition_t *)partition);
   for (size_t i = dst_offset; i < dst_offset + size; i++) {
      assert(((esp_partition_t *)partition)->erase_bit[i] == 1);
      ((esp_partition_t *)partition)->flash[i] = ((uint8_t *)src)[i - dst_offset];
      ((esp_partition_t *)partition)->erase_bit[i] = 0;
   }
   return ESP_OK;
}

static inline esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
   assert(partition);
   assert(offset % 16 == 0);
   assert(size % 16 == 0);
   assert(offset + size <= partition->size);
   esp_partition_init((esp_partition_t *)partition);
   memset((void*)(partition->flash + offset), 0xff, size);
   memset((void*)(partition->erase_bit + offset), 0x1, size);
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
