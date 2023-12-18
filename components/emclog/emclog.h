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
#include "utz.h"
#include "zones.h"

typedef double (*EmcDoubleColumn)(void);
typedef float (*EmcFloatColumn)(void);
typedef int (*EmcIntColumn)(void);
typedef uint32_t (*EmcUint32Column)(void);
typedef void(*EmcStrColumn)(char *, size_t);

typedef enum {
    EMC_TYPE_DOUBLE,
    EMC_TYPE_FLOAT,
    EMC_TYPE_INT,
    EMC_TYPE_UINT32,
    EMC_TYPE_STR,
} EmcColumnType;

typedef enum {
    EMC_FLAG_NONE = 0,
    EMC_FLAG_HEX = 1,
} EmcColumnFlag;

typedef struct {
	char *name;
    EmcColumnType type;
    EmcColumnFlag flag;
	char *fmt;
    union {
        EmcDoubleColumn dbl;
        EmcFloatColumn flt;
        EmcIntColumn i;
        EmcUint32Column u32;
        EmcStrColumn str;
    } u;
} EmcColumn;

#define EMC_LOG_MAX_COLUMNS 64
#define EMC_LOG_NOTE_SIZE 128

typedef struct {
    TaskHandle_t sock_task;
    int sock;
    char *sock_buf;
    uint32_t sock_retries;

    TaskHandle_t log_task;
    EmcColumn log_cols[EMC_LOG_MAX_COLUMNS];
    uint32_t log_colcount;
    uint32_t log_flags;
    char *log_filename;
    char *log_buf;
    FILE *log_fp;
    size_t log_size;
	char *log_note;
} EmcLogger;

EmcColumn *emclogger_add_float(EmcLogger *logger, char *name, EmcFloatColumn fn, EmcColumnFlag flag);
EmcColumn *emclogger_add_double(EmcLogger *logger, char *name, EmcDoubleColumn fn, EmcColumnFlag flag);
EmcColumn *emclogger_add_int(EmcLogger *logger, char *name, EmcIntColumn fn, EmcColumnFlag flag);
EmcColumn *emclogger_add_uint32(EmcLogger *logger, char *name, EmcUint32Column fn, EmcColumnFlag flag);
EmcColumn *emclogger_add_str(EmcLogger *logger, char *name, EmcStrColumn fn, EmcColumnFlag flag);
void emclogger_write_column(EmcColumn *col, char *buf, size_t size);

void emclogger_init(EmcLogger *logger);
void emclogger_start(EmcLogger *logger);
void emclogger_register_defaults(EmcLogger *logger);

#endif
