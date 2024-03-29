#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

#include "zaptec_protocol_serialisation.h"
#include "../../main/sessionHandler.h"
#include "../../main/DeviceInfo.h"

float GetFloat(uint8_t * input);
uint32_t GetUint32_t(uint8_t * input);
uint16_t GetUInt16(uint8_t * input);

void zaptecProtocolStart();
void dspic_periodic_poll_start();
void protocol_task_ctrl_debug(int state);

uint32_t GetMCUComErrors();

MessageType MCU_SendCommandId(uint16_t paramIdentifier);
MessageType MCU_SendCommandWithData(uint16_t paramIdentifier, const char *data, size_t length, uint8_t *errorCode);
MessageType MCU_SendUint8Parameter(uint16_t paramIdentifier, uint8_t data);
MessageType MCU_SendUint16Parameter(uint16_t paramIdentifier, uint16_t data);
MessageType MCU_SendUint32Parameter(uint16_t paramIdentifier, uint32_t data);
MessageType MCU_SendFloatParameter(uint16_t paramIdentifier, float data);
ZapMessage MCU_SendUint32WithReply(uint16_t paramIdentifier, uint32_t data);

MessageType MCU_ReadFloatParameter(uint16_t paramIdentifier);
ZapMessage MCU_ReadParameter(uint16_t paramIdentifier);
ZapMessage MCU_SendUint8WithReply(uint16_t paramIdentifier, uint8_t data);

void MCU_StartLedOverride();
void MCU_StopLedOverride();

int MCURxGetStackWatermark();
int MCUTxGetStackWatermark();

char * MCU_GetSwVersionString();

hw_speed_revision MCU_GetHwIdMCUSpeed();
hw_power_revision MCU_GetHwIdMCUPower();
bool IsUKOPENPowerBoardRevision();
bool HasTamperDetection();
bool IsProgrammableFPGAUsed();

float MCU_GetOPENVoltage();

char * MCU_GetGridTestString();
uint8_t MCU_GetSwitchState();
float MCU_GetEmeterTemperature(uint8_t phase);
float MCU_GetTemperaturePowerBoard(uint8_t sensor);

float MCU_GetVoltages(uint8_t phase);
float MCU_GetCurrents(uint8_t phase);

float MCU_GetPower();
float MCU_GetEnergy();

void MCU_AdjustMaximumEnergy();
void MCU_ClearMaximumEnergy();

int8_t MCU_GetChargeMode();
uint8_t MCU_GetChargeOperatingMode();
void SetTransitionOperatingModeState(enum ChargerOperatingMode newTransitionState);
enum ChargerOperatingMode GetTransitionOperatingModeState();

bool MCU_GetEmeterSnapshot(int param, uint8_t *source, float *ret);

uint32_t MCU_GetDebugCounter();
uint32_t MCU_GetWarnings();
uint8_t MCU_GetResetSource();
uint8_t MCU_ReadResetSource();
void MCU_UpdateUseZaptecFinishedTimeout();
float MCU_GetInstantPilotState();
bool IsChargingAllowed();

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

int16_t MCU_GetServoCheckParameter(int parameterDefinition);
bool MCU_ServoCheckRunning();
void MCU_ServoCheckClear();
void MCU_PerformServoCheck();
float MCU_GetHWCurrentActiveLimit();
float MCU_GetHWCurrentMaxLimit();

void MCU_GetOPENSamples(char * samples);
uint8_t MCU_GetRelayStates();
uint8_t MCU_GetRCDButtonTestStates();
void MCU_GetFPGAInfo(char *stringBuf, int maxTotalLen);
bool MCU_SendCommandServoForceUnlock();

float MCU_GetMaxInstallationCurrentSwitch();
uint8_t GetMaxCurrentConfigurationSource();

uint8_t MCU_UpdateOverrideGridType();
uint8_t MCU_GetOverrideGridType();
uint8_t MCU_UpdateIT3OptimizationState();
uint8_t MCU_GetIT3OptimizationState();

void SetEspNotification(uint16_t notification);
void ClearNotifications();
uint32_t GetCombinedNotificationsMasked();
uint32_t GetCombinedNotifications();
void SetFinalStopActiveStatus(uint8_t status);
uint8_t GetFinalStopActiveStatus();
bool MCU_IsReady();

bool MCU_GetAutoClearStatus(uint32_t *timeout, uint16_t *count, uint16_t *totalCount);

bool MCU_ClearWarning(uint32_t warning);

void mcu_simulate_charge_op_mode(int mode);

#endif /* PROTOCOL_TASK_H */
