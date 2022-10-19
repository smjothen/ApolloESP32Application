#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

#include "zaptec_protocol_serialisation.h"
#include "../../main/sessionHandler.h"

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

void MCU_StartLedOverride();
void MCU_StopLedOverride();

int MCURxGetStackWatermark();
int MCUTxGetStackWatermark();

char * MCU_GetSwVersionString();
uint8_t MCU_ReadHwIdMCUSpeed();
uint8_t MCU_GetHwIdMCUSpeed();

uint8_t MCU_ReadHwIdMCUPower();
uint8_t MCU_GetHwIdMCUPower();

char * MCU_GetGridTestString();
uint8_t MCU_GetSwitchState();
float MCU_GetEmeterTemperature(uint8_t phase);
float MCU_GetTemperaturePowerBoard(uint8_t sensor);

float MCU_GetVoltages(uint8_t phase);
float MCU_GetCurrents(uint8_t phase);

float MCU_GetPower();
float MCU_GetEnergy();
float MCU_GetMaximumEnergy();
void MCU_ClearMaximumEnergy();

int8_t MCU_GetChargeMode();
uint8_t MCU_GetChargeOperatingMode();
void SetTransitionOperatingModeState(enum ChargerOperatingMode newTransitionState);
enum ChargerOperatingMode GetTransitionOperatingModeState();

uint32_t MCU_GetDebugCounter();
uint32_t MCU_GetWarnings();
uint8_t MCU_GetResetSource();
char * MCU_GetGridTypeString();
uint8_t MCU_GetGridType();
float MCU_GetChargeCurrentUserMax();
void HOLD_SetPhases(int setPhases);
int HOLD_GetSetPhases();
uint8_t GetMaxPhases();
uint8_t MCU_GetCableType();

uint16_t MCU_GetPilotAvg();
uint16_t MCU_ProximityInst();

float MCU_ChargeCurrentInstallationMaxLimit();
float MCU_StandAloneCurrent();

uint16_t MCU_GetServoCheckParameter(int parameterDefinition);
bool MCU_ServoCheckRunning();
void MCU_ServoCheckClear();
void MCU_PerformServoCheck();
float MCU_GetHWCurrentActiveLimit();
float MCU_GetHWCurrentMaxLimit();

float MCU_GetMaxInstallationCurrentSwitch();
uint8_t GetMaxCurrentConfigurationSource();

uint8_t MCU_UpdateOverrideGridType();
uint8_t MCU_GetOverrideGridType();
uint8_t MCU_UpdateIT3OptimizationState();
uint8_t MCU_GetIT3OptimizationState();

void SetEspNotification(uint16_t notification);
void ClearNotifications();
uint32_t GetCombinedNotifications();
void SetFinalStopActiveStatus(uint8_t status);
uint8_t GetFinalStopActiveStatus();
bool MCU_IsReady();


#endif /* PROTOCOL_TASK_H */
