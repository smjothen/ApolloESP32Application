#ifndef __CALIBRATION_H__
#define __CALIBRATION_H__

#include <stdbool.h>

#include "sessionHandler.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define CALIBRATION_KEY "GoTestBenchChangeMe!"

#define FOREACH_CS(CS)                 \
    CS(Starting, 1)                    \
    CS(ContactCleaning, 14)            \
    CS(WarmingUp, 2)                   \
    CS(WarmupSteadyStateTemp, 3)       \
    CS(CalibrateCurrentOffset, 4)      \
    CS(CalibrateVoltageOffset, 5)      \
    CS(CalibrateVoltageGain, 6)        \
    CS(CalibrateCurrentGain, 7)        \
    CS(VerificationStart, 8)           \
    CS(VerificationRunning, 9)         \
    CS(VerificationDone, 10)           \
    CS(WriteCalibrationParameters, 11) \
    CS(CloseRelays, 13)                \
    CS(Done, 12)

#define FOREACH_CHS(CHS) \
    CHS(InProgress, 0)   \
    CHS(Complete, 1)     \
    CHS(Failed, 2)

#define FOREACH_CLS(CLS)    \
    CLS(InitRelays, 0)      \
    CLS(Stabilization, 1)   \
    CLS(InitCalibration, 2) \
    CLS(Calibrating, 3)     \
    CLS(Verify, 4)          \
    CLS(VerifyRMS, 5)       \
    CLS(CalibrationDone, 6)

#define CS_ENUM(A, B) A = B,
#define CS_STRING(A, B) [B] = #A,

typedef enum {
    FOREACH_CS(CS_ENUM)
} CalibrationState;

typedef enum {
    FOREACH_CLS(CS_ENUM)
} CalibrationStep;

typedef enum {
    FOREACH_CHS(CS_ENUM)
} ChargerState;

typedef enum {
    Default = 0,
    DisableTimeout = (1<<0),
    HighLevelCurrent = (1<<1),
    MediumLevelCurrent = (1<<2),
} WarmupSettings;

typedef enum {
    CALIBRATION_TYPE_NONE = 0,
    CALIBRATION_TYPE_VOLTAGE_OFFSET = 1,
    CALIBRATION_TYPE_VOLTAGE_GAIN = 2,            
    CALIBRATION_TYPE_CURRENT_OFFSET = 3,
    CALIBRATION_TYPE_CURRENT_GAIN = 4,
} CalibrationType;

typedef struct {
    int Socket;
    struct sockaddr_in ServAddr;
} CalibrationServer;

typedef enum {
    HPF_COEF_I = 0x6F, // S.23 (NV), Current input HPF coefficient. Positive values only
    HPF_COEF_V = 0x70, // S.23 (NV), Voltage input HPF coefficient. Positive values only
    V1_OFFS = 0x75, // S.23 (NV), Voltage Offset Calibration
    V2_OFFS = 0x76, // S.23 (NV), Voltage Offset Calibration
    V3_OFFS = 0x77, // S.23 (NV), Voltage Offset Calibration
    V1_GAIN = 0x78, // S.21 (NV), Voltage Gain Calibration. Positive values only
    V2_GAIN = 0x79, // S.21 (NV), Voltage Gain Calibration. Positive values only
    V3_GAIN = 0x7A, // S.21 (NV), Voltage Gain Calibration. Positive values only
    I1_OFFS = 0x7B, // S.23 (NV), Current Offset Calibration
    I2_OFFS = 0x7C, // S.23 (NV), Current Offset Calibration
    I3_OFFS = 0x7D, // S.23 (NV), Current Offset Calibration
    I1_GAIN = 0x7E, // S.21 (NV), Current Gain Calibration. Positive values only.
    I2_GAIN = 0x7F, // S.21 (NV), Current Gain Calibration. Positive values only.
    I3_GAIN = 0x80, // S.21 (NV), Current Gain Calibration. Positive values only.
    IARMS_OFF = 0x81, // S.23 (NV), RMS Current dynamic offset adjust. Positive values only.
    IBRMS_OFF = 0x82, // S.23 (NV), RMS Current dynamic offset adjust. Positive values only.
    ICRMS_OFF = 0x83, // S.23 (NV), RMS Current dynamic offset adjust. Positive values only.
} EMRegister;

typedef struct {
    int Run;
    int Seq;
    int LastSeq;
    uint32_t CalibrationId;
    TickType_t LastTick;

    int HaveServer;
    CalibrationServer Server;

    bool InitState;
    TickType_t InitTick;
    TickType_t StateTick;
    CalibrationState State;

    CalibrationStep CStep;
    ChargerState CState;

    enum ChargerOperatingMode Mode;

    TickType_t WarmupTick;
    uint32_t WarmupOptions;

    float I[3];
    TickType_t LastITick;

    float V[3];
    TickType_t LastVTick;

    float E;
    TickType_t LastETick;

    TickType_t StabilizationTick;
} CalibrationCtx;

void calibration_task(void *pvParameters);

#endif
