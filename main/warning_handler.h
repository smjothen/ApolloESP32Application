#ifndef __WARNING_HANDLER_H__
#define __WARNING_HANDLER_H__

#include <stdbool.h>

typedef void (*WarningCallback)(uint32_t mask, uint32_t count, uint32_t threshold);

bool warning_handler_install(uint32_t mask, WarningCallback cb);
bool warning_handler_tick(uint32_t warnings);

#endif
