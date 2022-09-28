#include <string.h>
#include <sys/param.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "protocol_task.h"
#include "calibration.h"
#include "calibration_emeter.h"

static const char *TAG = "CALIBRATION    ";

const char *calibration_state_to_string(CalibrationState state) {
    const char *_calibration_states[] = { FOREACH_CS(CS_STRING) };
    size_t max_state = sizeof (_calibration_states) / sizeof (_calibration_states[0]);
    if (state < 0 || state > max_state || !_calibration_states[state]) {
        return "UnknownCalibrationState";
    }
    return _calibration_states[state];
}

const char *calibration_step_to_string(CalibrationStep state) {
    const char *_calibration_steps[] = { FOREACH_CLS(CS_STRING) };
    size_t max_state = sizeof (_calibration_steps) / sizeof (_calibration_steps[0]);
    if (state < 0 || state > max_state || !_calibration_steps[state]) {
        return "UnknownStep";
    }
    return _calibration_steps[state];
}

const char *charger_state_to_string(ChargerState state) {
    const char *_charger_states[] = { FOREACH_CHS(CS_STRING) };
    size_t max_state = sizeof (_charger_states) / sizeof (_charger_states[0]);
    if (state < 0 || state > max_state || !_charger_states[state]) {
        return "UnknownChargerState";
    }
    return _charger_states[state];
}

bool calibration_ref_voltage_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[VOLTAGE_TICK] < pdMS_TO_TICKS(500);
}

bool calibration_ref_current_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[CURRENT_TICK] < pdMS_TO_TICKS(500);
}

bool calibration_ref_energy_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[ENERGY_TICK] < pdMS_TO_TICKS(500);
}

int calibration_phases_within(float *phases, float nominal, float range) {
    float min = nominal * (1.0 - range);
    float max = nominal * (1.0 + range);

    int count = 0;
    if (phases[0] >= min && phases[0] <= max) count++;
    if (phases[1] >= min && phases[1] <= max) count++;
    if (phases[2] >= min && phases[2] <= max) count++;

    return count;
}

double calibration_scale_emeter(CalibrationState state, double raw) {
    switch(state) {
        case CalibrateCurrentGain:
        case CalibrateCurrentOffset:
            return raw * emeter_get_fsi();
        case CalibrateVoltageGain:
        case CalibrateVoltageOffset:
            return raw * emeter_get_fsv();
        default:
            break;
    }

    ESP_LOGE(TAG, "Invalid state for scaling!");
    return 0.0;
}

double calibration_inv_scale_emeter(CalibrationState state, float raw) {
    switch(state) {
        case CalibrateCurrentGain:
        case CalibrateCurrentOffset:
            return raw / emeter_get_fsi();
        case CalibrateVoltageGain:
        case CalibrateVoltageOffset:
            return raw / emeter_get_fsv();
        default:
            break;
    }

    ESP_LOGE(TAG, "Invalid state for scaling!");
    return 0.0;
}

static void calibration_set_sim_vals(float *iv, float *vv, float i, float v) {
    for (int phase = 0; phase < 3; phase++) {
        iv[phase] = i;
        vv[phase] = v;
    }
}

bool calibration_get_emeter_snapshot(CalibrationCtx *ctx, uint8_t *source, float *iv, float *vv) {
    MessageType ret;

#ifdef CALIBRATION_SIMULATION

    switch(ctx->State) {
        case WarmingUp: calibration_set_sim_vals(iv, vv, 0.5, 230.0); break;
        default:        calibration_set_sim_vals(iv, vv, 5.0, 230.0); break;
    }

    return true;

#endif

    if ((ret = MCU_SendCommandId(CommandCurrentSnapshot)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send current snapshot command!");
        return false;
    }

    if (!MCU_GetEmeterSnapshot(ParamEmeterVoltageSnapshot, source, iv)) {
        ESP_LOGE(TAG, "Couldn't get current snapshot!");
        return false;
    }

    if ((ret = MCU_SendCommandId(CommandVoltageSnapshot)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send voltage snapshot command!");
        return false;
    }

    if (!MCU_GetEmeterSnapshot(ParamEmeterVoltageSnapshot, source, vv)) {
        ESP_LOGE(TAG, "Couldn't get voltage snapshot!");
        return false;
    }

    return true;
}

uint16_t calibration_read_samples(void) {
    ZapMessage msg = MCU_ReadParameter(ParamCalibrationSamples);
    if (msg.type == MsgReadAck && msg.identifier == ParamCalibrationSamples && msg.length == 2) {
        return GetUInt16(msg.data);
    }
    return 0;
}

bool calibration_read_average(CalibrationCtx *ctx, int phase, float *average) {
    (void)ctx;

#ifdef CALIBRATION_SIMULATION

    switch(ctx->State) {
        case CalibrateCurrentGain  : *average = 5.001234 / emeter_get_fsi(); break;
        case CalibrateVoltageGain  : *average = 230.0012 / emeter_get_fsv(); break;
        case CalibrateVoltageOffset: *average = 0.001234; break;
        case CalibrateCurrentOffset: *average = 0.001234; break;
        default: *average = 0.0; break;
    }

    return true;

#endif

    ZapMessage msg = MCU_ReadParameter(ParamCalibrationAveragePhase1 + phase);
    if (msg.type == MsgReadAck && msg.identifier == (ParamCalibrationAveragePhase1 + phase) && msg.length == 4) {
        *average = GetFloat(msg.data);
        return true;
    }
    return false;
}

uint16_t calibration_get_emeter_averages(CalibrationCtx *ctx, int wait_for_samples, float *averages) {
    uint16_t samples = calibration_read_samples();

    if (samples == wait_for_samples) {
        for (int phase = 0; phase < 3; phase++) {
            if (!calibration_read_average(ctx, phase, &averages[phase])) {
                ESP_LOGE(TAG, "Couldn't read phase %d average!", phase);
                return 0;
            }
        }
        return samples;
    } else {
        ESP_LOGE(TAG, "Unexpected samples: %d", samples);
    }

    return 0;
}

bool calibration_open_relays(CalibrationCtx *ctx) {
    MessageType ret;
    if ((ret = MCU_SendCommandId(CommandStopChargingFinal)) != MsgCommandAck) {
        return false;
    }

    if (!(ctx->Mode == CHARGE_OPERATION_STATE_STOPPED || ctx->Mode == CHARGE_OPERATION_STATE_PAUSED)) {
        return false;
    }

    return true;
}

bool calibration_close_relays(CalibrationCtx *ctx) {
    MessageType ret;
    if ((ret = MCU_SendCommandId(CommandResumeChargingMCU)) != MsgCommandAck) {
        return false;
    }

    if (ctx->Mode != CHARGE_OPERATION_STATE_CHARGING) {
        return false;
    }

    return true;
}

bool calibration_start_calibration_run(CalibrationCtx *ctx, CalibrationType type) {
    return MCU_SendUint8Parameter(ParamRunCalibration, type) == MsgWriteAck;
}


bool calibration_total_charge_power(CalibrationCtx *ctx, float *val) {
    ZapMessage msg = MCU_ReadParameter(ParamTotalChargePower);
    if (msg.length == 4 && msg.identifier == ParamTotalChargePower) {
        *val = GetFloat(msg.data);
        return true;
    }
    return false;
}

bool calibration_set_standalone(CalibrationCtx *ctx, int standalone) {
    return MCU_SendUint8Parameter(ParamIsStandalone, 1) == MsgWriteAck;
}

bool calibration_set_simplified_max_current(CalibrationCtx *ctx, float current) {
    return MCU_SendFloatParameter(ParamSimplifiedModeMaxCurrent, 32.0) == MsgWriteAck;
}

bool calibration_set_lock_cable(CalibrationCtx *ctx, int lock) {
    return MCU_SendUint8Parameter(LockCableWhenConnected, 0) == MsgWriteAck;
}

bool calibration_get_calibration_id(CalibrationCtx *ctx, uint32_t *id) {
    if (!MCU_GetMidStoredCalibrationId(id)) {
        return false;
    }
    return true;
}

