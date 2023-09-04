#ifndef __OFFLINE_LOG_H__
#define __OFFLINE_LOG_H__

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <stdint.h>
#include <cJSON.h>

void offline_log_append_energy(time_t timestamp, double energy);
int offline_log_attempt_send(void);
int offline_log_delete(void);
void offline_log_disable(void);

uint32_t crc32_normal(uint32_t crc, const void *buf, size_t size);
int deleteOfflineLog();

void send_diagnostics_status_notification(bool is_trigger);
void get_diagnostics_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data);

void offlineLog_disable(void);

#define OFFLINE_LOG_LEGACY_LOGGING

#ifdef OFFLINE_LOG_LEGACY_LOGGING

void offline_log_append_energy_legacy(time_t timestamp, double energy);

#endif

void setup_offline_log();

#endif /* __OFFLINE_LOG_H__ */
