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

    static float ref_energy_start = -1.0,
                 ref_energy_end = -1.0,
                 energy_start = -1.0,
                 energy_end = -1.0;

    switch(CAL_STATE(ctx)) {
        case VerificationStart:
            // We can assume the data message with energy gets handled prior to the tick that calls
            // this function...
            if (ref_energy_start == -1.0) {
                ref_energy_start = ctx->Ref.E;
                ESP_LOGI(TAG, "%s: Verification %d started with ref. energy %.1f kWh", calibration_state_to_string(ctx), ctx->VerTest, ctx->Ref.E);
            }
            break;
        case VerificationRunning:
            break;
        case VerificationDone:
            if (ref_energy_end == -1.0) {
                ref_energy_end = ctx->Ref.E;
                ESP_LOGI(TAG, "%s: Verification %d ended with ref. energy %.1f kWh", calibration_state_to_string(ctx), ctx->VerTest, ctx->Ref.E);
            }
            break;
        default:
            return false;
    }

    /*
    float expectedCurrent;
    int expectedPhases = 0;

    switch(ctx->VerTest) {
        case I_tr_3_phase_PF1:
        case I_tr_3_phase_PF0_5:
            expectedCurrent = 5.0;
            break;
        case I_tr_L1:
            expectedCurrent = 5.0;
            expectedPhases = 1;
            break;
        case I_tr_L2:
            expectedCurrent = 5.0;
            expectedPhases = 2;
            break;
        case I_max:
            expectedCurrent = 32.0;
            break;
    }
    */

    if (CAL_CSTATE(ctx) != InProgress) {
        return false;
    }

    if (CAL_STATE(ctx) == VerificationStart) {
        if (!calibration_close_relays(ctx)) {
            ESP_LOGE(TAG, "%s: Waiting for relays to close ...", calibration_state_to_string(ctx));
            return false;
        }

        // TODO: Retry a few times?
        float energy;
        if (!MCU_GetInterpolatedEnergyCounter(&energy)) {
            ESP_LOGE(TAG, "%s: Couldn't read energy counter ...", calibration_state_to_string(ctx));
            CAL_CSTATE(ctx) = Failed;
            return true;
        }
        
        energy_start = energy * 0.1;
        ESP_LOGI(TAG, "%s: Received start energy counter: %0.1f Wh", calibration_state_to_string(ctx), energy_start);

        CAL_CSTATE(ctx) = Complete;
        return true;
    }

    if (CAL_STATE(ctx) == VerificationRunning) {
        if (ref_energy_start == -1.0) {
            ESP_LOGE(TAG, "%s: Verification started without reference energy!", calibration_state_to_string(ctx));
            CAL_CSTATE(ctx) = Failed;
            return false;
        }

        CAL_CSTATE(ctx) = Complete;
        return true;
    }

    if (CAL_STATE(ctx) == VerificationDone) {
        float energy;

        if (energy_start == -1.0 || ref_energy_start == -1.0) {
            ESP_LOGE(TAG, "%s: Verification ended without ref. energy %f / %f!", calibration_state_to_string(ctx), energy_start, ref_energy_start);
            CAL_CSTATE(ctx) = Failed;
            return false;
        } else if (ref_energy_end == -1.0) {
            ESP_LOGI(TAG, "%s: Waiting for end ref. energy ...", calibration_state_to_string(ctx));
            return false;
        } else if (!MCU_GetInterpolatedEnergyCounter(&energy)) {
            ESP_LOGE(TAG, "%s: Couldn't read energy counter ...", calibration_state_to_string(ctx));
            CAL_CSTATE(ctx) = Failed;
            return false;
        }

        energy_end = energy * 0.1;
        ESP_LOGI(TAG, "%s: Received end energy counter: %0.1f Wh", calibration_state_to_string(ctx), energy_end);

        float charger_energy = energy_end - energy_start;
        float ref_energy = (ref_energy_end - ref_energy_start) * 1000.0;

        float error;
        if (ref_energy == 0.0) {
            error = charger_energy == 0.0 ? 0.0 : 1.0;
        } else {
            error = (charger_energy / ref_energy) - 1.0;
        }

#ifdef CALIBRATION_SIMULATION
        error = 0.0001;
#endif

        ESP_LOGI(TAG, "%s: Verification completed, Test %.3f Wh vs. Reference %.3f Wh, Error = %.3f", calibration_state_to_string(ctx), charger_energy, ref_energy, error);

        CAL_CSTATE(ctx) = Complete;

        ref_energy_start = -1.0;
        ref_energy_end = -1.0;
        energy_start = -1.0;
        energy_end = -1.0;

        return true;
    }

    return CAL_CSTATE(ctx) != state;
}

