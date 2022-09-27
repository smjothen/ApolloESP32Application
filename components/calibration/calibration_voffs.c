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

bool calibration_step_calibrate_voltage_offset(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;

    ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx->State), calibration_step_to_string(ctx->CStep));

    switch (ctx->CStep) {
        case InitRelays:
            if (!ctx->StabilizationTick) {
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

            ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(7000);
            STEP(Stabilization);

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->StabilizationTick) {
                ESP_LOGI(TAG, "%s: VOFFS HPF done", calibration_state_to_string(ctx->State));

                if (!emeter_write(HPF_COEF_V, 0)) {
                    ESP_LOGE(TAG, "Writing HPF_COEF_V = 0 failed!");
                    return false;
                }

                STEP(InitCalibration);
            }

            break;
        case InitCalibration:
            ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
            STEP(Calibrating);
            break;
        case Calibrating: {
            if (xTaskGetTickCount() < ctx->StabilizationTick) {
                break;
            }

            for (int phase = 0; phase < 3; phase++) {
                uint32_t rawOffset;

                if (!emeter_read(V1_OFFS + phase, &rawOffset)) {
                    ESP_LOGE(TAG, "Writing VOFFS(%d) failed!", phase);
                    return false;
                }

                double offset = snToFloat(rawOffset, 23);

                ESP_LOGI(TAG, "%s: VOFFS_HPF(%d) = %f", calibration_state_to_string(ctx->State), phase, offset);

                ctx->Params.VoltageOffset[phase] = offset;
            }

            STEP(Verify);
            break;
        }
        case Verify:
            STEP(VerifyRMS);
            break;
        case VerifyRMS:
            STEP(CalibrationDone);
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
