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

bool calibration_step_calibrate_current_gain(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;

    ESP_LOGI(TAG, "%s: %s %s ...", calibration_state_to_string(ctx->State), charger_state_to_string(ctx->CState), calibration_step_to_string(ctx->CStep));

    switch (ctx->CStep) {
        case InitRelays:
            if (!ctx->StabilizationTick) {

                if (!calibration_close_relays(ctx)) {
                    break;
                }

                for (int phase = 0; phase < 3; phase++) {
                    emeter_write_float(I1_GAIN + phase, 1.0, 21);
                    emeter_write_float(IARMS_OFF + phase, 0.0, 23);
                }
            }

            ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
            STEP(Stabilization);

            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->StabilizationTick) {
                STEP(InitCalibration);
            }

            break;
        case InitCalibration: {
            if (calibration_start_calibration_run(ctx, CALIBRATION_TYPE_CURRENT_GAIN)) {
                STEP(Calibrating);
            }

            break;
        }
        case Calibrating: {
            float avg[3];

            if (calibration_get_emeter_averages(ctx, EXPECTED_SAMPLES_GAIN, avg)) {
                ESP_LOGI(TAG, "%s: Averages %f %f %f", calibration_state_to_string(ctx->State), avg[0], avg[1], avg[2]);

                if (calibration_ref_current_is_recent(ctx)) {

                    for (int phase = 0; phase < 3; phase++) {
                        double averageMeasurement = calibration_scale_emeter(ctx->State, avg[phase]);
                        double gain = ctx->I[phase] / averageMeasurement;
                        ctx->Params.CurrentGain[phase] = gain;

                        ESP_LOGI(TAG, "%s: Current gain %d %f", calibration_state_to_string(ctx->State), phase, gain);
                    }
                    
                } else {
                    ESP_LOGI(TAG, "%s: Waiting for recent reference current", calibration_state_to_string(ctx->State));
                    break;
                }

                if (calibration_start_calibration_run(ctx, CALIBRATION_TYPE_CURRENT_GAIN)) {
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
