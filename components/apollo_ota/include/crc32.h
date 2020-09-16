#ifndef CRC32_H
#define CRC32_H
#include <stdint.h>
#include <stddef.h>

#ifdef	__cplusplus
extern "C" {
#endif
uint32_t crc32(uint32_t crc, const void *buf, size_t size);
uint32_t application_crc(uint32_t app_lenght_bytes);
#ifdef	__cplusplus
}
#endif

#endif /* CRC32_H */
