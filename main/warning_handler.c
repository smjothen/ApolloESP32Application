#include <stdlib.h>

#include "esp_log.h"

#include "warning_handler.h"
#include "DeviceInfo.h"
#include "zaptec_protocol_warnings.h"

// Handle any warnings that require resetting the MCU:
//
// WARNING_FPGA_VERSION
// WARNING_EMETER_LINK
//

static const char *TAG = "WARNING        ";

typedef struct {
	uint32_t warning_mask;
	uint32_t warning_count;
	uint32_t warning_threshold;
	uint32_t warning_base;
	uint32_t warning_events;
	WarningCallback warning_callback;
} WarningState;

#define WARNING_MAX_HANDLERS 32

static WarningState handlers[WARNING_MAX_HANDLERS];
static int handler_count = 0;

// Default reset after this amount of ticks (~seconds)
#define WARNING_THRESHOLD_DEFAULT 30
#define WARNING_THRESHOLD_MAX 86400

bool warning_handler_install(uint32_t mask, WarningCallback cb) {
	if (handler_count >= WARNING_MAX_HANDLERS) {
		return false;
	}

	WarningState *handler = &handlers[handler_count++];
	handler->warning_mask = mask;
	handler->warning_count = 0;
	handler->warning_events = 0;
	handler->warning_base = 3;
	handler->warning_threshold = WARNING_THRESHOLD_DEFAULT;
	handler->warning_callback = cb;

	return true;
}

void warning_handler_reset(void) {
	for (int i = 0; i < handler_count; i++) {
		WarningState *w = &handlers[i];
		w->warning_count = 0;
		w->warning_events = 0;
		w->warning_threshold = WARNING_THRESHOLD_DEFAULT;
	}
}

// 30 * 3^e gives handling of warning at 30s, 1.5m, 4.5m, 13.5m, 40.5m, 2.025h, 6.075h, 18.225h, 54.675h
// 30 * 2^e gives handling of warning at 30s, 1m, 2m, 4m, 8m, 16m, 32m, 1.06h, 2.13h, 4.26h, 8.53h, 17.06h, 34.13h
static uint32_t warning_handler_get_backoff(WarningState *w) {
	uint32_t backoff = WARNING_THRESHOLD_DEFAULT;

	for (uint32_t i = 0; i < w->warning_events; i++) {
		backoff *= w->warning_base;
	}

	if (backoff > WARNING_THRESHOLD_MAX) {
		backoff = WARNING_THRESHOLD_MAX;
	}

	return backoff;
}

bool warning_handler_tick(uint32_t warnings) {
	bool handled = false;

	for (size_t i = 0; i < handler_count; i++) {
		WarningState *w = &handlers[i];

		if (w->warning_mask & warnings) {
			ESP_LOGI(TAG, "Warning  %08" PRIX32 ": %" PRIu32 " / %" PRIu32 " / %" PRIu32, w->warning_mask, w->warning_count, w->warning_threshold, w->warning_events);

			if (w->warning_count == w->warning_threshold) {
				handled = true;

				w->warning_events++;
				w->warning_threshold = warning_handler_get_backoff(w);

				if (w->warning_callback) {
					// If handler given, run that
					w->warning_callback(w->warning_mask, w->warning_events, w->warning_threshold);
				}

				w->warning_count = 0;
			} else {
				w->warning_count++;
			}
		} else if (w->warning_count > 0) {
			//ESP_LOGI(TAG, "Reset    %08" PRIX32, w->warning_mask);
			w->warning_count = 0;
		}
	}

	return handled;
}
