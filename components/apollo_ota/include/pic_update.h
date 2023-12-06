#ifndef PIC_UPDATE_H
#define PIC_UPDATE_H

int update_dspic(void);
uint8_t get_bootloader_version(void);

bool is_goplus(void);
int update_goplus(void);

bool fpga_ensure_configured(void);

#endif /* PIC_UPDATE_H */
