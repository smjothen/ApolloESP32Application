#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

int at_command_at(void);
int at_command_echo_set(bool on);
int at_command_get_model_name(char *name, int buff_len);
int at_command_get_imei(char *imei, int buff_len);
int at_command_get_imsi(char *imsi, int buff_len);
int at_command_get_operator(char *operator, int buff_len);

int at_command_pdp_define(void);
int at_command_dial(void);

#endif /* AT_COMMANDS_H */
