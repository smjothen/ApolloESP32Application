#ifndef __CRC32_H__
#define __CRC32_H__

#include <stdint.h>
#include <stddef.h>

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);

#endif
