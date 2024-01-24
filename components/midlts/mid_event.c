#include "mid_event.h"

int mid_event_log_init(mid_event_log_t *log) {

	mid_event_log_entry_t *container = calloc(1, sizeof (mid_event_log_entry_t) * 16);
	if (!container) {
		return -1;
	}
	log->count = 0;
	log->capacity = 16;
	log->entries = container;
	return 0;
}

int mid_event_log_add(mid_event_log_t *log, mid_event_log_entry_t *entry) {
	if (!log || !log->entries) {
		return -1;
	}

	if (log->count >= log->capacity) {
		mid_event_log_entry_t *entries = calloc(1, sizeof (mid_event_log_entry_t) * log->capacity * 2);
		if (!entries) {
			return -1;
		}
		for (int i = 0; i < log->capacity; i++) {
			entries[i] = log->entries[i];
		}
		log->capacity *= 2;
		free(log->entries);
		log->entries = entries;
	}

	log->entries[log->count++] = *entry;
	return 0;
}

void mid_event_log_free(mid_event_log_t *log) {
	if (!log || !log->entries) {
		return;
	}

	free(log->entries);
	log->entries = NULL;
}
