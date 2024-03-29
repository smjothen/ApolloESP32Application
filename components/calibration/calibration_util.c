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
#include "mid.h"

static const char *TAG = "CALIBRATION    ";

double _test_currents[] = {
    [Starting] = 0.0,
    [WarmingUp] = 0.15, // Simulate overload for 32A warmup
    [WarmupSteadyStateTemp] = 0.0,
    [CalibrateCurrentOffset] = 0.0,
    [CalibrateVoltageOffset] = 0.0,
    [CalibrateVoltageGain] = 0.0,
    [CalibrateCurrentGain] = 5.0,
    [VerificationStart] = 0.0,
    [VerificationRunning] = 0.5,
    [VerificationDone] = 0.0,
    [WriteCalibrationParameters] = 0.0,
    [Done] = 0.0,
    [CloseRelays] = 0.0,
    [ContactCleaning] = 0.0
};

const char *calibration_mode_to_string(CalibrationMode mode) {
    switch(mode) {
        case Idle: return "Idle";
        case Open: return "Open";
        default: return "Closed";
    }
}

const char *calibration_state_to_string(CalibrationCtx *ctx) {
    CalibrationState state = ctx->State;
    const char *_calibration_states[] = { FOREACH_CS(CS_STRING) };
    size_t max_state = sizeof (_calibration_states) / sizeof (_calibration_states[0]);
    if (state < 0 || state > max_state || !_calibration_states[state]) {
        return "Unknown";
    }
    return _calibration_states[state];
}

const char *calibration_step_to_string(CalibrationCtx *ctx) {
    CalibrationStep state = ctx->CStep;
    const char *_calibration_steps[] = { FOREACH_CLS(CS_STRING) };
    size_t max_state = sizeof (_calibration_steps) / sizeof (_calibration_steps[0]);
    if (state < 0 || state > max_state || !_calibration_steps[state]) {
        return "Unknown";
    }
    return _calibration_steps[state];
}

const char *charger_state_to_string(CalibrationCtx *ctx) {
    CalibrationChargerState state = ctx->CState;
    const char *_charger_states[] = { FOREACH_CHS(CS_STRING) };
    size_t max_state = sizeof (_charger_states) / sizeof (_charger_states[0]);
    if (state < 0 || state > max_state || !_charger_states[state]) {
        return "Unknown";
    }
    return _charger_states[state];
}

bool calibration_ref_voltage_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[VOLTAGE_TICK] < pdMS_TO_TICKS(1000);
}

bool calibration_ref_current_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[CURRENT_TICK] < pdMS_TO_TICKS(1000);
}

bool calibration_get_ref_unit(CalibrationCtx *ctx, CalibrationUnit unit, int phase, float *value) {
    if (unit == UnitVoltage) {
        *value = ctx->Ref.V[phase];
        return calibration_ref_voltage_is_recent(ctx);
    } else {
        *value = ctx->Ref.I[phase];
        return calibration_ref_current_is_recent(ctx);
    }
}

bool calibration_ref_energy_is_recent(CalibrationCtx *ctx) {
    return xTaskGetTickCount() - ctx->Ticks[ENERGY_TICK] < pdMS_TO_TICKS(500);
}

double calibration_scale_emeter(CalibrationUnit unit, double raw) {
    switch(unit) {
        case UnitCurrent:
            return raw * emeter_get_fsi();
        default:
            return raw * emeter_get_fsv();
    }
}

double calibration_inv_scale_emeter(CalibrationUnit unit, float raw) {
    switch(unit) {
        case UnitCurrent:
            return raw / emeter_get_fsi();
        default:
            return raw / emeter_get_fsv();
    }
}

bool calibration_get_current_snapshot(CalibrationCtx *ctx, float *iv) {
    MessageType ret;

    if (calibration_is_simulation()) {
        iv[0] = iv[1] = iv[2] = _test_currents[ctx->State];
        return true;
    }

    if ((ret = MCU_SendCommandId(CommandCurrentSnapshot)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send current snapshot command!");
        return false;
    }

    uint8_t source;
    if (!MCU_GetEmeterSnapshot(ParamEmeterVoltageSnapshot, &source, iv)) {
        ESP_LOGE(TAG, "Couldn't get current snapshot!");
        return false;
    }

    return true;
}

bool calibration_get_emeter_snapshot(CalibrationCtx *ctx, uint8_t *source, float *iv, float *vv) {
    MessageType ret;

    if (calibration_is_simulation()) {
        iv[0] = iv[1] = iv[2] = _test_currents[ctx->State];
        vv[0] = vv[1] = vv[2] = 230.0;
        return true;
    }

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

bool calibration_read_average(CalibrationCtx *ctx, CalibrationType type, int phase, float *average) {
    (void)type;

    if (calibration_is_simulation()) {

        float currentOffset = 0.0;
        float voltageOffset = 0.0;
        float current;
        float voltage;

        enum ChargerOperatingMode mode = MCU_GetChargeOperatingMode();

        switch(mode) {
            case CHARGE_OPERATION_STATE_CHARGING:
                voltage = 230.0;
                current = _test_currents[ctx->State];
                break;
            default:
                voltage = 0.0;
                current = 0.0;
                break;
        }

        switch(type) {
            case CALIBRATION_TYPE_CURRENT_GAIN  : *average = current / emeter_get_fsi(); break;
            case CALIBRATION_TYPE_VOLTAGE_GAIN  : *average = voltage / emeter_get_fsv(); break;
            case CALIBRATION_TYPE_CURRENT_OFFSET: *average = currentOffset / emeter_get_fsi(); break;
            case CALIBRATION_TYPE_VOLTAGE_OFFSET: *average = voltageOffset / emeter_get_fsv(); break;
            default                             : *average = 0.0; break;
        }

        return true;
    }

    ZapMessage msg = MCU_ReadParameter(ParamCalibrationAveragePhase1 + phase);
    if (msg.type == MsgReadAck && msg.identifier == (ParamCalibrationAveragePhase1 + phase) && msg.length == 4) {
        *average = GetFloat(msg.data);
        return true;
    }
    return false;
}

uint16_t calibration_get_emeter_averages(CalibrationCtx *ctx, CalibrationType type, float *averages) {
    uint16_t samples = calibration_read_samples();

    uint16_t expected_samples;
    switch(type) {
        case CALIBRATION_TYPE_CURRENT_OFFSET:
        case CALIBRATION_TYPE_VOLTAGE_OFFSET:
            expected_samples = 100;
            break;
        default:
            expected_samples = 17;
            break;
    }

    if (samples == expected_samples) {
        for (int phase = 0; phase < 3; phase++) {
            if (!calibration_read_average(ctx, type, phase, &averages[phase])) {
                ESP_LOGE(TAG, "Couldn't read phase %d average!", phase);
                return 0;
            }
        }
        return samples;
    } else {
        ESP_LOGE(TAG, "Samples not ready? %d", samples);
    }

    return 0;
}

bool calibration_open_relays(CalibrationCtx *ctx) {
    MessageType ret;
    if ((ret = MCU_SendCommandId(CommandStopChargingFinal)) != MsgCommandAck) {
        return false;
    }

    return !(ctx->Flags & CAL_FLAG_RELAY_CLOSED);
}

bool calibration_close_relays(CalibrationCtx *ctx) {
    MessageType ret;
    if ((ret = MCU_SendCommandId(CommandResumeChargingMCU)) != MsgCommandAck) {
        return false;
    }

    return !!(ctx->Flags & CAL_FLAG_RELAY_CLOSED);
}

bool calibration_refresh(CalibrationCtx *ctx) {
    ZapMessage msg = MCU_ReadParameter(ParamContinueCalibration);
    if (msg.type == MsgReadAck && msg.identifier == ParamContinueCalibration && msg.length == 1) {
        return msg.data[0] == 1;
    }
    return false;
}

bool calibration_is_active(CalibrationCtx *ctx) {
    ZapMessage msg = MCU_ReadParameter(ParamStartCalibrations);
    if (msg.type == MsgReadAck && msg.identifier == ParamStartCalibrations && msg.length == 1) {
        return msg.data[0] == 1;
    }
    return false;
}

bool calibration_stop_mid_mode(CalibrationCtx *ctx) {
    return MCU_SendUint8Parameter(ParamStartCalibrations, 0x00) == MsgWriteAck;
}

bool calibration_start_mid_mode(CalibrationCtx *ctx) {
    return MCU_SendUint8Parameter(ParamStartCalibrations, 0x4D) == MsgWriteAck;
}

bool calibration_start_calibration_run(CalibrationType type) {
    return MCU_SendUint8Parameter(ParamRunCalibration, type) == MsgWriteAck;
}

bool calibration_read_warnings(uint32_t *warnings) {
    ZapMessage msg = MCU_ReadParameter(ParamWarnings);
    if (msg.type == MsgReadAck && msg.identifier == ParamWarnings && msg.length == 4) {
        *warnings = GetUint32_t(msg.data);
        return true;
    }
    *warnings = 0;
    return false;
}

bool calibration_read_mid_status(uint32_t *status) {
    return mid_get_status(status);
}


bool calibration_get_total_charge_power(CalibrationCtx *ctx, float *val) {

    if (calibration_is_simulation()) {
        ESP_LOGI(TAG, "%s: Simulating idle power!", calibration_state_to_string(ctx));
        *val = 25.0f;
        return true;
    }

    ZapMessage msg = MCU_ReadParameter(ParamTotalChargePower);
    if (msg.length == 4 && msg.identifier == ParamTotalChargePower) {
        *val = GetFloat(msg.data);
        return true;
    }
    return false;
}

// Blocks other LED activity
bool calibration_set_blinking(CalibrationCtx *ctx, int enabled) {
    return mid_set_blink_enabled(enabled);
}

bool calibration_turn_led_off(CalibrationCtx *ctx) {
    return MCU_SendUint8Parameter(ParamLedOverride, LED_STATE_OFF) == MsgWriteAck;
}

bool calibration_blink_led_green(CalibrationCtx *ctx) {
    return MCU_SendCommandId(CommandIndicateOk);
}

bool calibration_blink_led_red(CalibrationCtx *ctx) {
    return MCU_SendCommandId(CommandAuthorizationDenied);
}

bool calibration_set_standalone(CalibrationCtx *ctx, int standalone) {
    return MCU_SendUint8Parameter(ParamIsStandalone, standalone) == MsgWriteAck;
}

bool calibration_set_simplified_max_current(CalibrationCtx *ctx, float current) {
    return MCU_SendFloatParameter(ParamSimplifiedModeMaxCurrent, current) == MsgWriteAck;
}

bool calibration_set_lock_cable(CalibrationCtx *ctx, int lock) {
    return MCU_SendUint8Parameter(LockCableWhenConnected, lock) == MsgWriteAck;
}

bool calibration_get_calibration_id(CalibrationCtx *ctx, uint32_t *id) {
    if (!mid_get_calibration_id(id)) {
        ESP_LOGE(TAG, "Couldn't get calibration ID!");
        return false;
    }
    ESP_LOGI(TAG, "Got calibration ID %" PRIu32 "!", *id);
    return true;
}

bool calibration_write_parameter(CalibrationCtx *ctx, CalibrationType type, int phase, float value) {
    CalibrationParameter *params = NULL;

    switch (type) {
        case CALIBRATION_TYPE_CURRENT_GAIN: params = ctx->Params.CurrentGain; break;
        case CALIBRATION_TYPE_VOLTAGE_GAIN: params = ctx->Params.VoltageGain; break;
        case CALIBRATION_TYPE_CURRENT_OFFSET: params = ctx->Params.CurrentOffset; break;
        case CALIBRATION_TYPE_VOLTAGE_OFFSET: params = ctx->Params.VoltageOffset; break;
        default: break;
    }

    if (!params) {
        ESP_LOGE(TAG, "%s: Attempt to set invalid parameter type!", calibration_state_to_string(ctx));
        return false;
    }

    params[phase].value = value;
    params[phase].assigned = true;

    return true;
}

bool calibration_get_energy_counter(float *energy) {
    float buckets = 0.0;
    for (int i = 0; i < 5; i++) {
        if (mid_get_energy_interpolated(&buckets)) {
            // Returns -10.0 if reading the bucket fails
            if (buckets > -5.0) {
                *energy = buckets;
                return true;
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
    return false;
}

char *_calibration_fail(const char *format, va_list args) {
    int ret;
    char *ptr = NULL;

    ret = vasprintf(&ptr, format, args);

    if (ret < 0) {
        return NULL;
    }

    return ptr;
}

void calibration_fail(CalibrationCtx *ctx, const char *format, ...) {
    va_list ap;
    va_start(ap, format);

    if (ctx->FailReason) {
        free(ctx->FailReason);
        ctx->FailReason = NULL;
    }

    ctx->FailReason = _calibration_fail(format, ap);

    ESP_LOGE(TAG, "%s: %s", calibration_state_to_string(ctx), ctx->FailReason);
    CAL_CSTATE(ctx) = Failed;

    CALLOG(ctx, "- %s", ctx->FailReason);

    va_end(ap);
}
