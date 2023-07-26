#ifndef __OFFLINE_LOG_H__
#define __OFFLINE_LOG_H__

#include <stddef.h>
#include <time.h>
#include <stdint.h>

void offline_log_append_energy(time_t timestamp, double energy);
int offline_log_attempt_send(void);
int offline_log_delete(void);
void offline_log_disable(void);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);

#define OFFLINE_LOG_LEGACY_LOGGING

#ifdef OFFLINE_LOG_LEGACY_LOGGING

void offline_log_append_energy_legacy(time_t timestamp, double energy);

#endif
 
void setup_offline_log();

#endif /* __OFFLINE_LOG_H__ */
