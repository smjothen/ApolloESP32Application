#ifndef __CALIBRATION_H__
#define __CALIBRATION_H__

#include <stdbool.h>

#include "sessionHandler.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

// Comment out to disable simulated eMeter/calibration values
#define CALIBRATION_SIMULATION

#define CALIBRATION_KEY "GoTestBenchChangeMe!"

// TODO: Check these are reasonable?
#define CALIBRATION_IOFF_MAX_ERROR 0.002
#define CALIBRATION_VOFF_MAX_ERROR 0.002

#define CALIBRATION_IOFF_MAX_RMS 0.2  // 200mV
#define CALIBRATION_VOFF_MAX_RMS 0.08 // 80mV

#define CALIBRATION_IGAIN_MAX_ERROR 0.003 // 0.3%
#define CALIBRATION_VGAIN_MAX_ERROR 0.002 // 0.2%

#define CALIBRATION_SERVER_PORT 3333
#define CALIBRATION_SERVER_IP "232.10.11.12"

#define CALIBRATION_TIMEOUT 1000 

#define STATE(s) (ctx->CState = (s))
#define COMPLETE() STATE(Complete)
#define FAILED() STATE(Failed)

#define STEP(s) (ctx->CStep = (s))

#define FOREACH_CS(CS)                 \
    CS(Starting,                    1) \
    CS(WarmingUp,                   2) \
    CS(WarmupSteadyStateTemp,       3) \
    CS(CalibrateCurrentOffset,      4) \
    CS(CalibrateVoltageOffset,      5) \
    CS(CalibrateVoltageGain,        6) \
    CS(CalibrateCurrentGain,        7) \
    CS(VerificationStart,           8) \
    CS(VerificationRunning,         9) \
    CS(VerificationDone,           10) \
    CS(WriteCalibrationParameters, 11) \
    CS(Done,                       12) \
    CS(CloseRelays,                13) \
    CS(ContactCleaning,            14)

#define FOREACH_CHS(CHS) \
    CHS(InProgress, 0) \
    CHS(Complete,   1) \
    CHS(Failed,     2)

#define FOREACH_CLS(CLS)    \
    CLS(InitRelays,      0) \
    CLS(Stabilization,   1) \
    CLS(InitCalibration, 2) \
    CLS(Calibrating,     3) \
    CLS(Verify,          4) \
    CLS(VerifyRMS,       5) \
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
} CalibrationChargerState;

typedef enum {
    UnitVoltage = 0,
    UnitCurrent,
} CalibrationUnit;

typedef enum {
    Default = 0,
    DisableTimeout = (1<<0),
    HighLevelCurrent = (1<<1),
    MediumLevelCurrent = (1<<2),
} CalibrationWarmupSettings;

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
    int Initialized;
} CalibrationServer;

enum {
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
};

typedef enum {
    TICK = 0,
    STATE_TICK,
    CURRENT_TICK,
    VOLTAGE_TICK,
    ENERGY_TICK,
    WARMUP_TICK,
    STABILIZATION_TICK,
    LAST_TICK,
} CalibrationTickType;

typedef struct {
    bool assigned;
    double value;
} CalibrationParameter;

typedef struct {
    uint32_t CalibrationId;

    CalibrationParameter CurrentGain[3];
    CalibrationParameter VoltageGain[3];
    CalibrationParameter CurrentOffset[3];
    CalibrationParameter VoltageOffset[3];
} CalibrationParameters;

typedef struct {
    float I[3];
    float V[3];
    float E;
} CalibrationReference;

typedef struct {
    uint16_t crc;
    uint32_t v_gain[3];
    uint32_t v_offset[3];
    uint32_t i_gain[3];
    uint16_t functional_relay_revision;
    uint32_t calibration_id;
    uint16_t t_offs[3];
} __attribute__((packed)) CalibrationHeader;

typedef enum {
    CAL_FLAG_INIT         = (1 << 0),
    CAL_FLAG_WAIT_RESET   = (1 << 1),
    CAL_FLAG_RELAY_CLOSED = (1 << 2),
} CalibrationFlags;

typedef struct {
    int Run;
    int Seq;
    int LastSeq;
    int Count;

    CalibrationServer Server;
    CalibrationState State;
    CalibrationStep CStep;
    CalibrationChargerState CState;
    CalibrationWarmupSettings Warmup;
    CalibrationFlags Flags;
    CalibrationReference Ref;
    CalibrationParameters Params;

    TickType_t Ticks[LAST_TICK];
} CalibrationCtx;

void calibration_task(void *pvParameters);

bool calibration_step_calibrate_current_gain(CalibrationCtx *ctx);
bool calibration_step_calibrate_current_offset(CalibrationCtx *ctx);
bool calibration_step_calibrate_voltage_gain(CalibrationCtx *ctx);
bool calibration_step_calibrate_voltage_offset(CalibrationCtx *ctx);

bool calibration_get_total_charge_power(CalibrationCtx *ctx, float *val);
bool calibration_set_standalone(CalibrationCtx *ctx, int standalone);
bool calibration_set_simplified_max_current(CalibrationCtx *ctx, float current);
bool calibration_set_lock_cable(CalibrationCtx *ctx, int lock);
bool calibration_get_calibration_id(CalibrationCtx *ctx, uint32_t *id);

#endif
