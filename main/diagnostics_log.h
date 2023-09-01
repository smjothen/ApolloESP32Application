#ifndef DIAGNOSTICS_LOG
#define DIAGNOSTICS_LOG

#include "sdkconfig.h"

#ifdef CONFIG_ZAPTEC_DIAGNOSTICS_LOG
#include <stdio.h>

#include "esp_system.h"
#include "esp_log.h"

esp_err_t diagnostics_log_init();
esp_err_t diagnostics_log_deinit();
esp_err_t diagnostics_log_publish_as_event();
esp_log_level_t esp_log_level_from_char(char severity_char);
char char_from_esp_log_level(esp_log_level_t severity);
esp_err_t diagnostics_log_empty();
#endif /* CONFIG_ZAPTEC_DIAGNOSTICS_LOG */

#endif /* DIAGNOSTICS_LOG */
