#ifndef __MID_EVENT_H__
#define __MID_EVENT_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
	MID_EVENT_LOG_TYPE_INIT,
	MID_EVENT_LOG_TYPE_START,
	MID_EVENT_LOG_TYPE_SUCCESS,
	MID_EVENT_LOG_TYPE_FAIL,
	MID_EVENT_LOG_TYPE_ERASE,
} mid_event_log_type_t;

typedef struct {
	mid_event_log_type_t type;
	uint16_t seq;
	uint16_t data;
} mid_event_log_entry_t;

typedef struct {
} mid_event_log_container_t;

typedef struct {
	size_t count;
	size_t capacity;
	mid_event_log_entry_t *entries;
} mid_event_log_t;

int mid_event_log_init(mid_event_log_t *log);
int mid_event_log_add(mid_event_log_t *log, mid_event_log_entry_t *entry);
void mid_event_log_free(mid_event_log_t *);

#endif
