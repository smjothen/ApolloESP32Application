#ifndef PPP_TASK_H
#define PPP_TASK_H
#include "freertos/FreeRTOS.h"

#define GPIO_OUTPUT_PWRKEY		21
#define GPIO_OUTPUT_DTR			27
#define GPIO_OUTPUT_RESET		33
#define GPIO_OUTPUT_DEBUG_LED    0

void configure_uart(void);
int pppGetStackWatermark();
void ppp_task_start(void);

void cellularPinsInit();
void cellularPinsOn();
void cellularPinsOff();

void hard_reset_cellular(void);

#define LINE_BUFFER_SIZE 256
BaseType_t await_line(char *pvBuffer, TickType_t xTicksToWait);
int send_line(char *);
void clear_lines(void);

int enter_command_mode(void);
int enter_data_mode(void);

bool LteIsConnected();
int GetNumberAsString(char * inputString, char * outputString, int maxLength);
const char* LTEGetImei();
const char* LTEGetIccid();
const char* LTEGetImsi();

int configure_modem_for_prodtest(void (log_cb)(char *));

#endif /* PPP_TASK_H */
