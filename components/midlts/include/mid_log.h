#ifndef __MIDLOG_H__
#define __MIDLOG_H__

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <stdint.h>
#include "mid_log_unit.h"

struct _midlog_ctx_t;
typedef struct _midlog_ctx_t midlog_ctx_t;

typedef bool (*midlog_pub_t)(const char *);
typedef bool (*midlog_energy_t)(uint32_t *);

int midlog_init(midlog_ctx_t *ctx, midlog_pub_t publisher, midlog_energy_t energy);
int midlog_free(midlog_ctx_t *ctx);

int midlog_append_energy(midlog_ctx_t *ctx);
int midlog_attempt_send(midlog_ctx_t *ctx);

#endif /* __MIDLOG_H__ */
