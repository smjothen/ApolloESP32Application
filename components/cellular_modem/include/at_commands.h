#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

int at_command_at(void);
int at_command_with_ok_ack(char * command, uint32_t timeout_ms);
int at_command_echo_set(bool on);
int at_command_detect_echo(void);
int at_command_set_baud_high(void);
int at_command_set_baud_low(void);
int at_command_save_baud(void);
int at_command_get_model_name(char *name, int buff_len);
int at_command_get_imei(char *imei, int buff_len);
int at_command_get_ccid(char *ccid, int buff_len);
int at_command_get_imsi(char *imsi, int buff_len);
int at_command_get_operator(char *operator, int buff_len);
int at_command_get_cereg(char *cereg, int buff_len);
int at_command_get_qnwinfo(char *qnwinfo, int buff_len);

int at_command_pdp_define(void);
int at_command_dial(void);
int at_command_data_mode(void);

int at_command_signal_strength(char *sysmode, int *rssi, int *rsrp, int *sinr, int *rsrq);
int at_command_signal_quality(int *rssi, int *ber);

int at_command_flow_ctrl_enable(void);

int at_command_network_registration_status();
int at_command_registered(void);

int at_command_get_detailed_version(char *out, int out_len);

int at_command_status_pdp_context(void);
int at_command_activate_pdp_context(void);
int at_command_deactivate_pdp_context(void);

int at_command_http_test(void);
int at_command_ping_test(int *sent, int *rcvd, int *lost, int *min, int *max, int *avg);

int at_command_set_LTE_M_only_at_boot(void);
int at_command_set_LTE_M_only_immediate(void);

int at_command_get_LTE_M_only(char * reply, int buf_len);

int at_command_set_LTE_band_at_boot(void);
int at_command_set_LTE_band_immediate(void);

int at_command_get_LTE_band(char * reply, int buf_len);
int at_command_soft_restart(void);

int at_command_generic(char *atCommand, char * response, int buff_len);

#endif /* AT_COMMANDS_H */
