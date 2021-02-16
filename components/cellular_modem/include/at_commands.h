#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

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

int at_command_pdp_define(void);
int at_command_dial(void);
int at_command_data_mode(void);

int at_command_signal_strength(char *sysmode, int *rssi, int *rsrp, int *sinr, int *rsrq);
int at_command_signal_quality(int *rssi, int *ber);

int at_command_flow_ctrl_enable(void);

int at_command_network_registration_status();

int at_command_activate_pdp_context(void);
int at_command_deactivate_pdp_context(void);
int at_command_ping_test(int *sent, int *rcvd, int *lost, int *min, int *max, int *avg);

#endif /* AT_COMMANDS_H */
