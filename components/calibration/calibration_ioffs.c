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

void calibration_write_current_offset(CalibrationCtx *ctx, float *avg) {
    for (int phase = 0; phase < 3; phase++) {
        double offset = avg[phase] / EMETER_SYS_GAIN;
        ctx->Params.CurrentOffset[phase] = offset;

        ESP_LOGI(TAG, "%s: Offset %d %f %f", calibration_state_to_string(ctx->State), phase, avg[phase], calibration_scale_emeter(ctx->State, offset));
    }
}

bool calibration_step_calibrate_current_offset(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;

    ESP_LOGI(TAG, "%s: %s %s ...", calibration_state_to_string(ctx->State), charger_state_to_string(ctx->CState), calibration_step_to_string(ctx->CStep));

    switch (ctx->CStep) {
        case InitRelays:
            if (!ctx->StabilizationTick) {

                if (!calibration_open_relays(ctx)) {
                    break;
                }

                emeter_write_float(HPF_COEF_I, 0.0, 23);

                for (int phase = 0; phase < 3; phase++) {
                    emeter_write_float(IARMS_OFF + phase, 0.0, 23);
                    emeter_write_float(I1_OFFS + phase, 0.0, 23);
                    emeter_write_float(I1_GAIN + phase, 1.0, 21);
                }
            }

            ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(0);
            STEP(Stabilization);

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->StabilizationTick) {
                STEP(InitCalibration);
            }

            break;
        case InitCalibration: {
            if (calibration_start_calibration_run(ctx, CALIBRATION_TYPE_CURRENT_OFFSET)) {
                STEP(Calibrating);
            }

            break;
        }
        case Calibrating: {
            float avg[3];

            if (calibration_get_emeter_averages(ctx, EXPECTED_SAMPLES_OFFSET, avg)) {
                ESP_LOGI(TAG, "%s: Averages %f %f %f", calibration_state_to_string(ctx->State), avg[0], avg[1], avg[2]);

                calibration_write_current_offset(ctx, avg);

                if (calibration_start_calibration_run(ctx, CALIBRATION_TYPE_CURRENT_OFFSET)) {
                    STEP(Verify);
                }
            }

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
