#include <string.h>
#include <sys/param.h>
#include <stdbool.h>
#include <math.h>
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

bool calibration_step_calibrate_voltage_offset(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;
    /* CalibrationType type = CALIBRATION_TYPE_VOLTAGE_OFFSET; */
    /* CalibrationType extra_type = CALIBRATION_TYPE_VOLTAGE_GAIN; */
    /* CalibrationUnit unit = UnitVoltage; */
    float max_error = CALIBRATION_VOFF_MAX_ERROR;

    ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx->State), calibration_step_to_string(ctx->CStep));

    switch (ctx->CStep) {
        case InitRelays:
            if (!ctx->Ticks[STABILIZATION_TICK]) {
                if (!calibration_close_relays(ctx)) {
                    break;
                }
            }

            for (int phase = 0; phase < 3; phase++) {
                if (!emeter_write_float(V1_OFFS + phase, 0.0, 23)) {
                    ESP_LOGE(TAG, "Writing VOFFS(%d) failed!", phase);
                    return false;
                }
            }

            if (!emeter_write(HPF_COEF_V, 0x020000)) {
                ESP_LOGE(TAG, "Writing HPF_COEF_V = 0x020000 failed!");
                return false;
            }


            ESP_LOGI(TAG, "%s: VOFFS HPF started", calibration_state_to_string(ctx->State));

            ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(7000);
            STEP(Stabilization);

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->Ticks[STABILIZATION_TICK]) {
                ESP_LOGI(TAG, "%s: VOFFS HPF done", calibration_state_to_string(ctx->State));

                if (!emeter_write(HPF_COEF_V, 0)) {
                    ESP_LOGE(TAG, "Writing HPF_COEF_V = 0 failed!");
                    return false;
                }

                STEP(InitCalibration);
            }

            break;
        case InitCalibration:
            ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
            STEP(Calibrating);
            break;
        case Calibrating: {
            if (xTaskGetTickCount() < ctx->Ticks[STABILIZATION_TICK]) {
                break;
            }

            for (int phase = 0; phase < 3; phase++) {
                uint32_t rawOffset;

                if (!emeter_read(V1_OFFS + phase, &rawOffset)) {
                    ESP_LOGE(TAG, "VOFFS(%d) write failed!", phase);
                    return false;
                }

                double offset = snToFloat(rawOffset, 23);

                ESP_LOGI(TAG, "%s: VOFFS(%d) = %f", calibration_state_to_string(ctx->State), phase, offset);

                ctx->Params.VoltageOffset[phase] = offset;
            }

            STEP(Verify);
            break;
        }
        case Verify: {
            // Just simple verification that offset is reasonable
            for (int phase = 0; phase < 3; phase++) {
                uint32_t rawOffset;

                if (!emeter_read(V1_OFFS + phase, &rawOffset)) {
                    ESP_LOGE(TAG, "%s: VOFFS(%d) read failed!", calibration_state_to_string(ctx->State), phase);
                    return false;
                }

                float offset = snToFloat(rawOffset, 23);

                if (fabsf(offset) < max_error) {
                    ESP_LOGI(TAG, "%s: VOFFS(%d) = %f  < %f", calibration_state_to_string(ctx->State), phase, fabsf(offset), max_error);
                } else {
                    ESP_LOGE(TAG, "%s: VOFFS(%d) = %f >= %f", calibration_state_to_string(ctx->State), phase, fabsf(offset), max_error);
                    FAILED();
                    return false;
                }
            }
 
            STEP(CalibrationDone);
            break;
        }
        case VerifyRMS:
            ESP_LOGE(TAG, "%s: Shouldn't be here!", calibration_state_to_string(ctx->State));
            FAILED();
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
