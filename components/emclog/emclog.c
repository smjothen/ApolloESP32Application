#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "emclog.h"

#define EMC_LOG_BUF_SIZE 2048

#define EMC_LOG_FLAG_DISABLE 1
#define EMC_LOG_FLAG_INITED 2

#define PORT 8585

static const char *TAG = "EMCLOG         ";

static void emclogger_write_socket(EmcLogger *logger, const char *buf, bool block) {
    if (logger->sock < 0) {
        return;
    }

    int len = strlen(buf);
    int remaining = len;
    int retries = block ? logger->sock_retries : 0;

    while (remaining > 0) {
        int written = send(logger->sock, buf + (len - remaining), remaining, 0);

        if (written < 0) {
            if ((errno == EWOULDBLOCK || errno == EAGAIN) && retries-- > 0) {
                vTaskDelay(10);
                continue;
            } else {
                ESP_LOGE(TAG, "Too many send() retries, aborting (%d, %d)!", written, errno);
                return;
            }
        }

        remaining -= written;
    }
}

static void emclogger_print_log(EmcLogger *logger) {
    FILE *f = fopen(logger->log_filename, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cant open %s", logger->log_filename);
        return;
    }

    while (1) {
        size_t read = fread(logger->log_buf, 1, EMC_LOG_BUF_SIZE - 1, f);
        logger->log_buf[read] = 0;

        emclogger_write_socket(logger, logger->log_buf, true);

        if (read < EMC_LOG_BUF_SIZE - 1) {
            break;
        }

        vTaskDelay(1);
    }

    fclose(f);
}

static bool emclogger_tick(EmcLogger *logger) {
    if (!(logger->log_flags & EMC_LOG_FLAG_INITED)) {
        ESP_LOGE(TAG, "EMC logging not initialized!");
        return false;
    }

    if (logger->log_flags & EMC_LOG_FLAG_DISABLE) {
        ESP_LOGI(TAG, "EMC logging disabled!");
        if (logger->log_fp) {
            fflush(logger->log_fp);
            fsync(fileno(logger->log_fp));
            fclose(logger->log_fp);
            logger->log_fp = NULL;
        }
        return false;
    }

    if (!logger->log_fp) {
        ESP_LOGI(TAG, "Opening EMC log %s", logger->log_filename);
        logger->log_fp = fopen(logger->log_filename, "a");
    }

    if (!logger->log_fp) {
        ESP_LOGE(TAG, "Couldn't open EMC file %s errno = %s", logger->log_filename, strerror(errno));
        return false;
    }

    long size = ftell(logger->log_fp);
    if (size >= logger->log_size) {
        // Seems to be some corruption in wear levelling component when filesystem gets near full
        ESP_LOGE(TAG, "Filesystem full, reset log!");
        logger->log_flags |= EMC_LOG_FLAG_DISABLE;
        return false;
    }

    char *ptr = logger->log_buf;
    char buf[64];

    if (!size) {
		// First line, write the header first
		for (uint32_t i = 0; i < logger->log_colcount; i++) {
			 EmcColumn *col = &logger->log_cols[i];
			 ptr += sprintf(ptr, "%s,", col->name);
		}
		ptr += sprintf(ptr, "LogSize,");
		ptr += sprintf(ptr, "Note\n");
    }

    for (uint32_t i = 0; i < logger->log_colcount; i++) {
		 EmcColumn *col = &logger->log_cols[i];
		 emclogger_write_column(col, buf, sizeof (buf));
		 ptr += sprintf(ptr, "%s,", buf);
	}

	ptr += sprintf(ptr, "%ld,", size);

	char *note = logger->log_note;
	if (note) {
		ptr += sprintf(ptr, "%s", note);
		free(note);
		logger->log_note = note = NULL;
	}

    ESP_LOGI(TAG, "%s", logger->log_buf);

    ptr += sprintf(ptr, "\n");

    size_t nitems = ptr - logger->log_buf;
    size_t wrote = fwrite(logger->log_buf, nitems, 1, logger->log_fp);

    if (!wrote) {
        ESP_LOGE(TAG, "Error logging EMC data %s", strerror(errno));
    }

    int ret = fflush(logger->log_fp);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error fflush %s", strerror(errno));
    }

    ret = fsync(fileno(logger->log_fp));
    if (ret == -1) {
        ESP_LOGE(TAG, "Error fsync %s", strerror(errno));
    }

    emclogger_write_socket(logger, logger->log_buf, false);

    return true;
}

#define EMC_LOG_START          0
#define EMC_LOG_STOP           1
#define EMC_LOG_DELETE         2
#define EMC_LOG_DUMP           3
#define EMC_LOG_NOTE           4

static void emclogger_handle_socket(EmcLogger *logger) {
    const char *_sock_cmds[] = {
        [EMC_LOG_START ] = "start",
        [EMC_LOG_STOP  ] = "stop",
        [EMC_LOG_DELETE] = "del",
        [EMC_LOG_DUMP  ] = "dump",
        [EMC_LOG_NOTE  ] = "note ",
    };

    while (1) {
        int len = recv(logger->sock, logger->sock_buf, EMC_LOG_BUF_SIZE - 1, 0);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(1);
            } else {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                break;
            }
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            char *buf = logger->sock_buf;
            buf[len] = 0;

			for (int j = len - 1; j >= 0; j--) {
				if (isspace((int)buf[j])) {
					buf[j] = 0;
				} else {
					break;
				}
			}

            for (size_t i = 0; i < sizeof (_sock_cmds) / sizeof (_sock_cmds[0]); i++) {
				size_t cmd_len = strlen(_sock_cmds[i]);
                if (strncmp(buf, _sock_cmds[i], cmd_len) == 0) {
					if (i == EMC_LOG_NOTE) {
						logger->log_note = strdup(buf + cmd_len);
						break;
					}
					xTaskNotify(logger->log_task, i, eSetValueWithOverwrite);
                }
            }
        }
    }

    logger->sock = -1;
}

static void emclogger_socket_task(void *pvParameters) {
    EmcLogger *logger = (EmcLogger *)pvParameters;

    bool first = false;

    int err;
    int listen_sock;

    while (!network_WifiIsConnected()) {
    	ESP_LOGI(TAG, "Waiting for Wifi to connect");
    	vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    while (1) {
        struct sockaddr_in dest;
        dest.sin_addr.s_addr = htonl(INADDR_ANY);
        dest.sin_family = AF_INET;
        dest.sin_port = htons(PORT);

        if(!first) {
        	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

          if (listen_sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
          }

          ESP_LOGI(TAG, "Socket created");

          int err = bind(listen_sock, (struct sockaddr *)&dest, sizeof (dest));
          if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
          }

          ESP_LOGI(TAG, "Socket binded");
        }

        err = listen(listen_sock, 3);

        if (err != 0) {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source;
        socklen_t addr_len = sizeof(source);
        int socket = accept(listen_sock, (struct sockaddr *)&source, &addr_len);

        if (socket < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        fcntl(socket, F_SETFL, O_NONBLOCK);

        ESP_LOGI(TAG, "Socket accepted");

        logger->sock = socket;
        emclogger_handle_socket(logger);

    		first = true;

        if (err < 0) {
            ESP_LOGE(TAG, "Shutting down socket to allow new connection: %d", errno);
            shutdown(socket, 2);
            close(socket);
            socket = -1;
            break;
        }
    }

    vTaskDelete(NULL);
}

static void emclogger_log_task(void *pvParameter) {
    EmcLogger *logger = (EmcLogger *)pvParameter;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    while (1) {
        uint32_t val = 0xFFFFFFFF;

        if (xTaskNotifyWait(0, ULONG_MAX, &val, 1) == pdPASS) {
            if (val == EMC_LOG_START) {
                logger->log_flags &= ~EMC_LOG_FLAG_DISABLE;
            } else if (val == EMC_LOG_STOP) {
                logger->log_flags |= EMC_LOG_FLAG_DISABLE;
            } else if (val == EMC_LOG_DELETE) {
                logger->log_flags |= EMC_LOG_FLAG_DISABLE;
                // Tick to close file
                emclogger_tick(logger);
                int ret = remove(logger->log_filename);
				ESP_LOGI(TAG, "Removing %s = %d", logger->log_filename, ret);
            } else if (val == EMC_LOG_DUMP) {
                logger->log_flags |= EMC_LOG_FLAG_DISABLE;
                // Tick to close file
                emclogger_tick(logger);
                emclogger_print_log(logger);
            }
        }

        emclogger_tick(logger);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }

    vTaskDelete(NULL);
}

static EmcColumn *emclogger_add(EmcLogger *logger, EmcColumn col) {
    if (logger->log_colcount >= EMC_LOG_MAX_COLUMNS) {
        ESP_LOGE(TAG, "Too many columns!");
        return NULL;
    }

    EmcColumn *column = &logger->log_cols[logger->log_colcount++];
	*column = col;

	return column;
}

void emclogger_write_column(EmcColumn *col, char *buf, size_t size) {
	switch (col->type) {
		case EMC_TYPE_INT:
			snprintf(buf, size - 1, "%d", col->u.i());
			break;
		case EMC_TYPE_FLOAT:
			snprintf(buf, size - 1, "%.1f", col->u.flt());
			break;
		case EMC_TYPE_DOUBLE:
			snprintf(buf, size - 1, "%.1f", col->u.dbl());
			break;
		case EMC_TYPE_UINT32:
			if (col->flag & EMC_FLAG_HEX) {
				snprintf(buf, size - 1, "%" PRIX32, col->u.u32());
			} else {
				snprintf(buf, size - 1, "%" PRIu32, col->u.u32());
			}
			break;
		case EMC_TYPE_STR:
			col->u.str(buf, size);
			break;
	}
}

EmcColumn *emclogger_add_float(EmcLogger *logger, char *name, EmcFloatColumn fn, EmcColumnFlag flag) {
    EmcColumn col = { .name = name, .type = EMC_TYPE_FLOAT, .flag = flag, .u.flt = fn };
    return emclogger_add(logger, col);
}

EmcColumn *emclogger_add_double(EmcLogger *logger, char *name, EmcDoubleColumn fn, EmcColumnFlag flag) {
    EmcColumn col = { .name = name, .type = EMC_TYPE_DOUBLE, .flag = flag, .u.dbl = fn };
    return emclogger_add(logger, col);
}

EmcColumn *emclogger_add_int(EmcLogger *logger, char *name, EmcIntColumn fn, EmcColumnFlag flag) {
    EmcColumn col = { .name = name, .type = EMC_TYPE_INT, .flag = flag, .u.i = fn };
    return emclogger_add(logger, col);
}

EmcColumn *emclogger_add_uint32(EmcLogger *logger, char *name, EmcUint32Column fn, EmcColumnFlag flag) {
    EmcColumn col = { .name = name, .type = EMC_TYPE_UINT32, .flag = flag, .u.u32 = fn };
    return emclogger_add(logger, col);
}

EmcColumn *emclogger_add_str(EmcLogger *logger, char *name, EmcStrColumn fn, EmcColumnFlag flag) {
    EmcColumn col = { .name = name, .type = EMC_TYPE_STR, .flag = flag, .u.str = fn };
    return emclogger_add(logger, col);
}

void emclogger_init(EmcLogger *logger) {
    memset(logger, 0, sizeof (*logger));

    logger->sock = -1;
    logger->sock_buf = calloc(1, EMC_LOG_BUF_SIZE);
    logger->sock_retries = 16;

    logger->log_filename = "/files/emc.log";
    logger->log_buf = calloc(1, EMC_LOG_BUF_SIZE);
    // Files partition is ~1.6MB, dedicate 1MB of this for EMC logging
    logger->log_size = 1024 * 1024;
    logger->log_flags = EMC_LOG_FLAG_INITED;
    logger->log_colcount = 0;
	logger->log_note = NULL;
}

void emclogger_start(EmcLogger *logger) {
    xTaskCreate(emclogger_socket_task, "emc_socket_task", 2560, logger, 5, &logger->sock_task);
    xTaskCreate(emclogger_log_task, "emc_log_task", 3584, logger, 8, &logger->log_task);
}
