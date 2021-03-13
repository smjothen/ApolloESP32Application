#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

#include "zaptec_protocol_serialisation.h"

float GetFloat(uint8_t * input);

void zaptecProtocolStart();
void dspic_periodic_poll_start();
void protocol_task_ctrl_debug(int state);

uint32_t GetMCUComErrors();

MessageType MCU_SendCommandId(uint16_t paramIdentifier);
MessageType MCU_SendUint8Parameter(uint16_t paramIdentifier, uint8_t data);
MessageType MCU_SendUint16Parameter(uint16_t paramIdentifier, uint16_t data);
MessageType MCU_SendUint32Parameter(uint16_t paramIdentifier, uint32_t data);
MessageType MCU_SendFloatParameter(uint16_t paramIdentifier, float data);

MessageType MCU_ReadFloatParameter(uint16_t paramIdentifier);
ZapMessage MCU_ReadParameter(uint16_t paramIdentifier);

int MCURxGetStackWatermark();
int MCUTxGetStackWatermark();

char * MCU_GetSwVersionString();
char * MCU_GetGridTestString();
uint8_t MCU_GetSwitchState();
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
char * MCU_GetGridTypeString();
uint8_t MCU_GetGridType();
float MCU_GetChargeCurrentUserMax();
void HOLD_SetPhases(int setPhases);
int HOLD_GetSetPhases();
uint8_t MCU_GetCableType();

uint16_t MCU_GetPilotAvg();
uint16_t MCU_ProximityInst();

float MCU_ChargeCurrentInstallationMaxLimit();
float MCU_StandAloneCurrent();

float MCU_GetMaxInstallationCurrentSwitch();
void SetEspNotification(uint16_t notification);
uint32_t GetCombinedNotifications();


#endif /* PROTOCOL_TASK_H */
