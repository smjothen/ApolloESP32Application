#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

void zaptecProtocolStart();
float MCU_GetTemperature();
float MCU_GetVoltages(uint8_t phase);
float MCU_GetCurrents(uint8_t phase);

#endif /* PROTOCOL_TASK_H */
