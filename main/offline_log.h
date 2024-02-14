#ifndef __OFFLINE_LOG_H__
#define __OFFLINE_LOG_H__

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <stdint.h>
#include <cJSON.h>
#include "crc32.h"
#include "DeviceInfo.h"

int offline_log_attempt_send(void);
int offline_log_delete(void);
void offline_log_init(void);

#ifdef CONFIG_ZAPTEC_GO_PLUS
void offline_log_append_energy(uint32_t pos);
#else
void offline_log_append_energy(time_t timestamp, double energy);
#endif

void send_diagnostics_status_notification(bool is_trigger);
void get_diagnostics_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data);

#endif /* __OFFLINE_LOG_H__ */
