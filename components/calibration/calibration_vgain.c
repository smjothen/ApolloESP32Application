#include <string.h>
#include <sys/param.h>
#include <math.h>
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
#include "calibration_util.h"
#include "calibration_emeter.h"

static const char *TAG = "CALIBRATION    ";

bool calibration_step_calibrate_voltage_gain(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;
    CalibrationType type = CALIBRATION_TYPE_VOLTAGE_GAIN;
    CalibrationUnit unit = UnitVoltage;
    float max_error = CALIBRATION_VGAIN_MAX_ERROR;

    ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx->State), calibration_step_to_string(ctx->CStep));

    switch (ctx->CStep) {
        case InitRelays:
            if (!ctx->Ticks[STABILIZATION_TICK]) {
                if (!calibration_close_relays(ctx)) {
                    break;
                }

                for (int phase = 0; phase < 3; phase++) {
                    emeter_write_float(V1_GAIN + phase, 1.0, 21);
                }
            }

            ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
            STEP(Stabilization);

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->Ticks[STABILIZATION_TICK]) {
                STEP(InitCalibration);
            }

            break;
        case InitCalibration: {

            if (calibration_start_calibration_run(type)) {
                STEP(Calibrating);
            }

            break;
        }
        case Calibrating: {

            float avg[3];

            if (calibration_get_emeter_averages(type, avg)) {
                if (calibration_ref_voltage_is_recent(ctx)) {

                    for (int phase = 0; phase < 3; phase++) {
                        double averageMeasurement = calibration_scale_emeter(unit, avg[phase]);
                        double gain = ctx->Ref.V[phase] / averageMeasurement;
                        ctx->Params.VoltageGain[phase] = gain;

                        ESP_LOGI(TAG, "%s: VGAIN(%d) = %f (%f / %f)", calibration_state_to_string(ctx->State), phase, gain, ctx->Ref.V[phase], averageMeasurement);

                        if (!emeter_write_float(V1_GAIN + phase, gain, 21)) {
                            ESP_LOGE(TAG, "%s: VGAIN(%d) write failed!", calibration_state_to_string(ctx->State), phase);
                            return false;
                        }
                    }

                } else {
                    ESP_LOGI(TAG, "%s: VGAIN voltage reference too old. Waiting ...", calibration_state_to_string(ctx->State));
                    break;
                }

                if (calibration_start_calibration_run(type)) {
                    ctx->VerificationCount = 0;
                    STEP(Verify);
                }
            }

            break;
        }
        case Verify: {

            float avg[3];

            if (calibration_get_emeter_averages(type, avg)) {
                for (int phase = 0; phase < 3; phase++) {
                    float average = calibration_scale_emeter(unit, avg[phase]);

                    float reference;
                    if (!calibration_get_ref_unit(ctx, unit, phase, &reference)) {
                        ESP_LOGE(TAG, "%s: VGAIN reference voltage too old. Waiting ...", calibration_state_to_string(ctx->State));
                        return false;
                    }

                    float error = fabsf(1.0f - (reference / average));

                    if (error < max_error) {
                        ESP_LOGI(TAG, "%s: VGAIN(%d) = %f  < %f", calibration_state_to_string(ctx->State), phase, error, max_error);
                    } else {
                        ESP_LOGE(TAG, "%s: VGAIN(%d) = %f >= %f", calibration_state_to_string(ctx->State), phase, error, max_error);
                        FAILED();
                        return false;
                    }
                }

                if (++ctx->VerificationCount >= 5) {
                    ctx->VerificationCount = 0;
                    STEP(CalibrationDone);
                } else {
                    calibration_start_calibration_run(type);
                }
            }

            break;
        }
        case VerifyRMS:
            // No RMS verification for gains
            break;
        case CalibrationDone:
            // Reset
            STEP(InitRelays);
            // Complete state
            COMPLETE();
            break;
    }

    return ctx->CStep != step;
}
