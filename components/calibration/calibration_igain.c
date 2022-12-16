#include <string.h>
#include <math.h>
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
#include "calibration_util.h"
#include "calibration_emeter.h"

static const char *TAG = "CALIBRATION    ";

bool calibration_step_calibrate_current_gain(CalibrationCtx *ctx) {
    CalibrationStep step = CAL_STEP(ctx);
    CalibrationType type = CALIBRATION_TYPE_CURRENT_GAIN;
    CalibrationUnit unit = UnitCurrent;
    float max_error = CALIBRATION_IGAIN_MAX_ERROR;

    ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx), calibration_step_to_string(ctx));

    switch (CAL_STEP(ctx)) {
        case InitRelays:
            if (!calibration_set_mode(ctx, Closed)) {
                return false;
            }

            for (int phase = 0; phase < 3; phase++) {
                if (!emeter_write_float(I1_GAIN + phase, 1.0, 21)) {
                    ESP_LOGE(TAG, "Writing IGAIN(%d) failed!", phase);
                }
                if (!emeter_write_float(IARMS_OFF + phase, 0.0, 23)) {
                    ESP_LOGE(TAG, "Writing IARMS(%d) failed!", phase);
                }
            }

            ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
            CAL_STEP(ctx) = Stabilization;

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->Ticks[STABILIZATION_TICK]) {
                CAL_STEP(ctx) = InitCalibration;
            }

            break;
        case InitCalibration: {
            if (calibration_start_calibration_run(type)) {
                CAL_STEP(ctx) = Calibrating;
            }

            break;
        }
        case Calibrating: {
            float avg[3];

            if (calibration_get_emeter_averages(type, avg)) {
                if (calibration_ref_current_is_recent(ctx)) {
                    for (int phase = 0; phase < 3; phase++) {
                        double average = calibration_scale_emeter(unit, avg[phase]);
                        double gain = ctx->Ref.I[phase] / average;

                        calibration_write_parameter(ctx, type, phase, gain);

                        ESP_LOGI(TAG, "%s: IGAIN(%d) = %f = (%f / %f)", calibration_state_to_string(ctx), phase, gain, ctx->Ref.I[phase], average);

                        if (!emeter_write_float(I1_GAIN + phase, gain, 21)) {
                            ESP_LOGE(TAG, "%s: IGAIN(%d) write failed!", calibration_state_to_string(ctx), phase);
                            return false;
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "%s: IGAIN current reference too old. Waiting ...", calibration_state_to_string(ctx));
                    break;
                }

                if (calibration_start_calibration_run(type)) {
                    ctx->Count = 0;
                    CAL_STEP(ctx) = Verify;
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
                        ESP_LOGI(TAG, "%s: IGAIN reference current too old. Waiting ...", calibration_state_to_string(ctx));
                        return false;
                    }

                    float error = fabsf(1.0f - (reference / average));

                    if (error < max_error) {
                        ESP_LOGI(TAG, "%s: IGAIN(%d) = %f  < %f", calibration_state_to_string(ctx), phase, error, max_error);
                    } else {
                        ESP_LOGE(TAG, "%s: IGAIN(%d) = %f >= %f", calibration_state_to_string(ctx), phase, error, max_error);
                        calibration_error_append(ctx, "Current gain too large for L%d: %f >= %f", phase + 1, average, max_error);
                        CAL_CSTATE(ctx) = Failed;
                        return false;
                    }
                }

                if (ctx->Count++ >= CALIBRATION_VERIFY_TIMES) {
                    ctx->Count = 0;
                    CAL_STEP(ctx) = CalibrationDone;
                } else {
                    calibration_start_calibration_run(type);
                }
            }

            break;

        }
        case VerifyRMS:
            // No RMS verification for gains
            ESP_LOGE(TAG, "%s: Shouldn't be here!", calibration_state_to_string(ctx));
            CAL_CSTATE(ctx) = Failed;
            break;
        case CalibrationDone:
            // Reset
            CAL_STEP(ctx) = InitRelays;
            // Complete state
            CAL_CSTATE(ctx) = Complete;
            break;
    }

    return CAL_STEP(ctx) != step;
}
