#ifndef __EMETER_H__
#define __EMETER_H__

#include <stdint.h>
#include <stdbool.h>

#define EMETER_SYS_GAIN 5.275001

double snToFloat(uint32_t data, uint16_t radix);
uint32_t floatToSn(double data, uint16_t radix);

void emeter_write(uint8_t reg, int registerValue);
void emeter_write_float(uint8_t reg, double value, int radix);
uint32_t emeter_read(uint8_t reg);
double emeter_get_fsv(void);
double emeter_get_fsi(void);
	
#endif
