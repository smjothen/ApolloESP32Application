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

bool calibration_tick_verification(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    float expectedCurrent = 0;
    CalibrationOverload expectedPhases = All;

    switch (ctx->VerTest) {
        case NoLoad:
            break;
        case StartingCurrent:
            expectedCurrent = 0.025;
            break;
        case I_min:
            expectedCurrent = 0.25;
            break;
        case I_tr_L1:
            expectedCurrent = 5.0;
            expectedPhases = L1;
            break;
        case I_tr_L2:
            expectedCurrent = 5.0;
            expectedPhases = L2;
            break;
        case I_tr_3_phase_PF0_5:
        case I_tr_3_phase_PF1:
            expectedCurrent = 5.0;
            break;
        case I_max:
            expectedCurrent = 32.0;
            break;
        case PreFlashVerification:
        case I_min_pre:
            expectedCurrent = 0.025;
            break;
        case I_tr_L3:
            expectedCurrent = 5.0;
            expectedPhases = L3;
            break;
    }

    // Relays should be closed anyway, but ...
    if (!calibration_close_relays(ctx)) {
        ESP_LOGE(TAG, "%s: Waiting for relays to close ...", calibration_state_to_string(ctx));
        return false;
    }

    if (CAL_CSTATE(ctx) != InProgress) {
        return false;
    }

    if (CAL_STATE(ctx) == VerificationStart) {
        if (!ctx->Ticks[VERIFICATION_TICK]) {
            ctx->Ticks[VERIFICATION_TICK] = xTaskGetTickCount();
        }

        float energy;
        if (!calibration_get_energy_counter(&energy)) {
            ESP_LOGI(TAG, "%s: Couldn't read start charger energy (busy?) ...", calibration_state_to_string(ctx));
            return true;
        }

        float ref_energy;
        if (ctx->Ticks[ENERGY_TICK] < ctx->Ticks[VERIFICATION_TICK]) {
            ESP_LOGI(TAG, "%s: Couldn't read start reference energy (too old) ...", calibration_state_to_string(ctx));
            return true;
        }

        ref_energy = ctx->Ref.E;

        ctx->Ref.CE[0] = energy * 0.1; // Impulses => Wh
        ctx->Ref.RE[0] = ref_energy * 1000.0; // kWh => Wh

        ESP_LOGI(TAG, "%s: Received start charger & reference energy (%f / %f)", calibration_state_to_string(ctx), ctx->Ref.CE[0], ctx->Ref.RE[0]);

        ctx->Ticks[VERIFICATION_TICK] = 0;

        CAL_CSTATE(ctx) = Complete;
        return true;
    }

    if (CAL_STATE(ctx) == VerificationRunning) {

        ctx->Ref.OverloadIsEstimated = true;
        ctx->Ref.OverloadCurrent = expectedCurrent;
        ctx->Ref.OverloadPhases = expectedPhases;

        // Always complete?
        CAL_CSTATE(ctx) = Complete;
        return true;
    }

    if (CAL_STATE(ctx) == VerificationDone) {
        if (!ctx->Ticks[VERIFICATION_TICK]) {
            ctx->Ticks[VERIFICATION_TICK] = xTaskGetTickCount();
        }

        float energy;
        if (!calibration_get_energy_counter(&energy)) {
            ESP_LOGI(TAG, "%s: Couldn't read end charger energy (busy?) ...", calibration_state_to_string(ctx));
            return true;
        }

        float ref_energy;
        if (ctx->Ticks[ENERGY_TICK] < ctx->Ticks[VERIFICATION_TICK]) {
            ESP_LOGI(TAG, "%s: Couldn't read end reference energy (too old) ...", calibration_state_to_string(ctx));
            return true;
        }

        ref_energy = ctx->Ref.E;

        ctx->Ref.CE[1] = energy * 0.1; // Impulses => Wh
        ctx->Ref.RE[1] = ref_energy * 1000.0; // kWh => Wh

        ESP_LOGI(TAG, "%s: Received end charger & reference energy (%f / %f)", calibration_state_to_string(ctx), ctx->Ref.CE[1], ctx->Ref.RE[1]);

        energy = ctx->Ref.CE[1] - ctx->Ref.CE[0];
        ref_energy = ctx->Ref.RE[1] - ctx->Ref.RE[0];

        float error;
        if (ref_energy == 0.0) {
            error = energy == 0.0 ? 0.0 : 1.0;
        } else {
            error = (energy / ref_energy) - 1.0;
        }

        error = fabs(error);

#ifdef CALIBRATION_SIMULATION

#ifdef CALIBRATION_SIMULATION_FAIL
        error = 1.0000;
#else
        error = 0.0000;
#endif

#endif

        float max_error = 0.0;

        switch (ctx->VerTest) {
            case NoLoad:
                break;
            case StartingCurrent:
                max_error = 0.2;
                break;
            case I_min:
                max_error = 0.02;
                break;
            case I_tr_L1:
            case I_tr_L2:
            case I_tr_L3:
                max_error = 0.0075;
                break;
            case I_tr_3_phase_PF0_5:
                max_error = 0.009;
                break;
            case I_tr_3_phase_PF1:
            case I_max:
            case PreFlashVerification:
            case I_min_pre:
                max_error = 0.005;
                break;
        }

        int id = ctx->VerTest;
        
        if (ctx->VerTest == NoLoad) {
            if (energy > 0.1) {
                ESP_LOGE(TAG, "%s: %d FAIL %.3fWh > 0.1Wh with no load!", calibration_state_to_string(ctx), id, energy);
                calibration_error_append(ctx, "Verification with no load failed, registered %.1fWh", energy);
                CAL_CSTATE(ctx) = Failed;
            } else {
                ESP_LOGI(TAG, "%s: %d PASS %.3fWh < 0.1Wh with no load!", calibration_state_to_string(ctx), id, energy);
                CAL_CSTATE(ctx) = Complete;
            }
        } else if (ctx->VerTest == I_min_pre) {
            // I_min_pre not really for verification, so don't fail
            CAL_CSTATE(ctx) = Complete;
        } else {
            if (error > max_error) {
                ESP_LOGE(TAG, "%s: %d FAIL %.3fWh vs. %.3fWh, Err = %.3f%% >  %.3f%%", calibration_state_to_string(ctx), id, energy, ref_energy, error * 100.0, max_error * 100.0);
                calibration_error_append(ctx, "Verification %d failed, %.3f vs %.3f, error too high (%.3f%% > %.3f%%)", id, energy, ref_energy, error * 100.0, max_error * 100.0);
                CAL_CSTATE(ctx) = Failed;
            } else {
                ESP_LOGI(TAG, "%s: %d PASS %.3fWh vs. %.3fWh, Err = %.3f%% <= %.3f%%", calibration_state_to_string(ctx), id, energy, ref_energy, error * 100.0, max_error * 100.0);
                CAL_CSTATE(ctx) = Complete;
            }
        }

        ctx->Ref.CE[0] = 0.0;
        ctx->Ref.RE[0] = 0.0;
        ctx->Ref.CE[1] = 0.0;
        ctx->Ref.RE[1] = 0.0;
        ctx->Ticks[VERIFICATION_TICK] = 0;

        ctx->Ref.OverloadIsEstimated = false;

        return true;
    }

    return CAL_CSTATE(ctx) != state;
}

