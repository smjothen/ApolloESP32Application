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
#include "calibration_util.h"
#include "calibration_emeter.h"

static const char *TAG = "CALIBRATION    ";

bool calibration_step_calibrate_current_offset(CalibrationCtx *ctx) {
    CalibrationStep step = CAL_STEP(ctx);
    CalibrationType type = CALIBRATION_TYPE_CURRENT_OFFSET;
    CalibrationType extra_type = CALIBRATION_TYPE_CURRENT_GAIN;
    CalibrationUnit unit = UnitCurrent;

    ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx), calibration_step_to_string(ctx));

    switch (CAL_STEP(ctx)) {
        case InitRelays:
            if (!ctx->Ticks[STABILIZATION_TICK]) {

                if (!calibration_open_relays(ctx)) {
                    break;
                }

                if (!emeter_write_float(HPF_COEF_I, 0.0, 23)) {
                    ESP_LOGE(TAG, "Writing HPF_COEF_I = 0.0 failed!");
                    return false;
                }

                for (int phase = 0; phase < 3; phase++) {
                    if (!emeter_write_float(IARMS_OFF + phase, 0.0, 23)) {
                        ESP_LOGE(TAG, "Writing IARMS(%d) = 0.0 failed!", phase);
                        return false;
                    }
                    if (!emeter_write_float(I1_OFFS + phase, 0.0, 23)) {
                        ESP_LOGE(TAG, "Writing IOFFS(%d) = 0.0 failed!", phase);
                        return false;
                    }
                    if (!emeter_write_float(I1_GAIN + phase, 1.0, 21)) {
                        ESP_LOGE(TAG, "Writing IGAIN(%d) = 0.0 failed!", phase);
                        return false;
                    }
                }
            }

            ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
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
                for (int phase = 0; phase < 3; phase++) {
                    double offset = avg[phase] / EMETER_SYS_GAIN;

                    ESP_LOGI(TAG, "%s: IOFFS(%d) = %f (%f)", calibration_state_to_string(ctx), phase, avg[phase], calibration_scale_emeter(unit, offset));

                    calibration_write_parameter(ctx, type, phase, offset);

                    if (!emeter_write_float(I1_OFFS + phase, offset, 23)) {
                        ESP_LOGE(TAG, "%s: IOFFS(%d) write failed!", calibration_state_to_string(ctx), phase);
                        return false;
                    }
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

            // Verify offset as well
            if (calibration_get_emeter_averages(type, avg)) {
                for (int phase = 0; phase < 3; phase++) {
                    float average = calibration_scale_emeter(unit, avg[phase]);
                    ESP_LOGI(TAG, "%s: IOFFS(%d) Verification = %f (%f)", calibration_state_to_string(ctx), phase, average, avg[phase]);
                }

                if (++ctx->Count >= 5) {
                    ctx->Count = 0;

                    if (calibration_start_calibration_run(extra_type)) {
                        CAL_STEP(ctx) = VerifyRMS;
                    }
                } else {
                    calibration_start_calibration_run(type);
                }
            }

            break;
        }
 
        case VerifyRMS: {
            float avg[3];

            // Verify RMS gain as well
            if (calibration_get_emeter_averages(extra_type, avg)) {
                float max_error = CALIBRATION_IOFF_MAX_RMS;

                for (int phase = 0; phase < 3; phase++) {
                    float average = calibration_scale_emeter(unit, avg[phase]);
                    if (average < max_error) {
                        ESP_LOGI(TAG, "%s: IOFFS(%d) = %f  < %f", calibration_state_to_string(ctx), phase, average, max_error);
                    } else {
                        ESP_LOGE(TAG, "%s: IOFFS(%d) = %f >= %f", calibration_state_to_string(ctx), phase, average, max_error);
                        CAL_CSTATE(ctx) = Failed;
                        return false;
                    }
                }

                if (++ctx->Count >= 5) {
                    ctx->Count = 0;
                    CAL_STEP(ctx) = CalibrationDone;
                } else {
                    calibration_start_calibration_run(extra_type);
                }
            }

            break;
        }
        case CalibrationDone:
            // Reset
            CAL_STEP(ctx) = InitRelays;
            // Complete state
            CAL_CSTATE(ctx) = Complete;
            break;
    }

    return CAL_STEP(ctx) != step;
}
