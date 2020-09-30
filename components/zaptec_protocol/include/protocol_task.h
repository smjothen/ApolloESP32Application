#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

void zaptecProtocolStart();
void dspic_periodic_poll_start();
void MCU_SendParameter(uint16_t paramIdentifier, float data);
//void MCU_SendParameter(uint16_t paramIdentifier, uint8_t * data, uint16_t length);

int MCU_GetSwitchState();

float MCU_GetEmeterTemperature(uint8_t phase);
float MCU_GetTemperaturePowerBoard(uint8_t sensor);
float MCU_GetTemperature();
float MCU_GetVoltages(uint8_t phase);
float MCU_GetCurrents(uint8_t phase);

float MCU_GetPower();
float MCU_GetEnergy();

uint8_t MCU_GetchargeMode();
uint8_t MCU_GetChargeOperatingMode();

uint32_t MCU_GetDebugCounter();
uint32_t MCU_GetWarnings();
uint8_t MCU_GetResetSource();

float MCU_GetMaxInstallationCurrentSwitch();


#endif /* PROTOCOL_TASK_H */
