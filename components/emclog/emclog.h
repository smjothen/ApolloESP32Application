#ifndef __EMCLOG_H__
#define __EMCLOG_H__

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "network.h"
#include "protocol_task.h"
#include "i2cDevices.h"
#include "CLRC661.h"

#include "utz.h"
#include "zones.h"

typedef double (*emc_double_col_t)(void);
typedef float (*emc_float_col_t)(void);
typedef int (*emc_int_col_t)(void);
typedef uint32_t (*emc_uint32_col_t)(void);
typedef void(*emc_str_col_t)(char *, size_t);

typedef enum {
    EMC_TYPE_DOUBLE,
    EMC_TYPE_FLOAT,
    EMC_TYPE_INT,
    EMC_TYPE_UINT32,
    EMC_TYPE_STR,
} emc_column_type_t;

typedef enum {
    EMC_FLAG_NONE = 0,
    EMC_FLAG_HEX = 1,
} emc_column_flag_t;

typedef struct {
	char *name;
    emc_column_type_t type;
    emc_column_flag_t flag;
	char *fmt;
    union {
        emc_double_col_t dbl;
        emc_float_col_t flt;
        emc_int_col_t i;
        emc_uint32_col_t u32;
        emc_str_col_t str;
    } u;
} emc_column_t;

#define EMC_LOG_MAX_COLUMNS 64
#define EMC_LOG_NOTE_SIZE 128

typedef struct {
    TaskHandle_t sock_task;
    int sock;
    char *sock_buf;
    uint32_t sock_retries;

    TaskHandle_t log_task;
    emc_column_t log_cols[EMC_LOG_MAX_COLUMNS];
    uint32_t log_colcount;
    uint32_t log_flags;
    char *log_filename;
    char *log_buf;
	char *log_note;
    size_t log_size;
    FILE *log_fp;
} emc_log_t;

emc_column_t *emclogger_add_float(emc_log_t *logger, char *name, emc_float_col_t fn, emc_column_flag_t flag);
emc_column_t *emclogger_add_double(emc_log_t *logger, char *name, emc_double_col_t fn, emc_column_flag_t flag);
emc_column_t *emclogger_add_int(emc_log_t *logger, char *name, emc_int_col_t fn, emc_column_flag_t flag);
emc_column_t *emclogger_add_uint32(emc_log_t *logger, char *name, emc_uint32_col_t fn, emc_column_flag_t flag);
emc_column_t *emclogger_add_str(emc_log_t *logger, char *name, emc_str_col_t fn, emc_column_flag_t flag);
void emclogger_write_column(emc_column_t *col, char *buf, size_t size);

void emclogger_init(emc_log_t *logger);
void emclogger_start(emc_log_t *logger);

#endif
