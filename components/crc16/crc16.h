#ifndef __CALIBRATION_CRC_H__
#define __CALIBRATION_CRC_H__

#include <stdint.h>

uint16_t crc16(uint16_t crc, uint8_t *data, uint16_t length);

#endif
