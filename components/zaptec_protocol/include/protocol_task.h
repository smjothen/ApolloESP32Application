#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

#include "zaptec_protocol_serialisation.h"
void zaptecProtocolStart();

MessageType MCU_SendCommandId(uint16_t paramIdentifier);
MessageType MCU_SendUint8Parameter(uint16_t paramIdentifier, uint8_t data);
MessageType MCU_SendUint16Parameter(uint16_t paramIdentifier, uint16_t data);
MessageType MCU_SendUint32Parameter(uint16_t paramIdentifier, uint32_t data);
MessageType MCU_SendFloatParameter(uint16_t paramIdentifier, float data);

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
