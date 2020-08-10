#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

void zaptecProtocolStart();
float MCU_GetEmeterTemperature(uint8_t phase);
float MCU_GetTemperaturePowerBoard(uint8_t sensor);
float MCU_GetTemperature();
float MCU_GetVoltages(uint8_t phase);
float MCU_GetCurrents(uint8_t phase);

float MCU_GetPower();
float MCU_GetEnergy();

uint8_t MCU_GetchargeMode();
uint8_t MCU_GetChargeOperatingMode();

#endif /* PROTOCOL_TASK_H */
