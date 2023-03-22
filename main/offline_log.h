#ifndef OFFLINE_LOG
#define OFFLINE_LOG
#include <stddef.h>
#include <stdint.h>

void setup_offline_log();

void append_offline_energy(int timestamp, double energy);
int attempt_log_send(void);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);
int deleteOfflineLog();
void offlineLog_disable(void);

#endif /* OFFLINE_LOG */
