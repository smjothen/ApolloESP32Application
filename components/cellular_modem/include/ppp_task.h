#ifndef PPP_TASK_H
#define PPP_TASK_H
#include "freertos/FreeRTOS.h"

int pppGetStackWatermark();
void ppp_task_start(void);
void hard_reset_cellular(void);

#define LINE_BUFFER_SIZE 256
BaseType_t await_line(char *pvBuffer, TickType_t xTicksToWait);
int send_line(char *);
void clear_lines(void);

int enter_command_mode(void);
int enter_data_mode(void);

bool LteIsConnected();

#endif /* PPP_TASK_H */
