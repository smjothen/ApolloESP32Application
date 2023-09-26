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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "network.h"
#include "../../main/DeviceInfo.h"
#include "i2cDevices.h"
#include "protocol_task.h"
#include "connectivity.h"
#include "sessionHandler.h"
#include "zaptec_protocol_serialisation.h"
#include "zaptec_cloud_observations.h"
#include "offlineSession.h"
#include "offline_log.h"

#include "calibration_crc.h"
#include "calibration_util.h"
#include "calibration_emeter.h"
#include "calibration_mid.h"

#include <calibration-message.pb.h>
#include <calibration.h>
#include <calibration_util.h>

#include <pb_decode.h>
#include <pb_encode.h>

static const char *TAG = "CALIBRATION    ";

static TaskHandle_t handle = NULL;

// Moved out of functions to save stack space
static char raddr_name[32] = { 0 };
static char buf[256] = { 0 };

// Buffer must be large enough for N serials (8 at Sanmina was too large
// for a 128 byte buffer)
static char recvbuf[512] = { 0 };

static char hexbuf[256] = { 0 };

static CalibrationCtx ctx = { 0 };
static CalibrationServer serv = { 0 };
static CalibrationUdpMessage msg = CalibrationUdpMessage_init_zero;

static fd_set fds = { 0 };

struct DeviceInfo devInfo;

static bool calibration_is_simulated = false;

void calibration_set_simulation(bool sim) {
    calibration_is_simulated = sim;
}

bool calibration_is_simulation(void) {
    return calibration_is_simulated;
}

void calibration_log_line(CalibrationCtx *ctx, const char *format, ...) {
    FILE *fp;
    if (!(fp = fopen(CALIBRATION_LOG, "a"))) {
        ESP_LOGE(TAG, "Can't open file for logging");
        return;
    }

    va_list ap;
    va_start(ap, format);
    vfprintf(fp, format, ap);
    va_end(ap);

    fclose(fp);
}

void calibration_log_dump(void) {
    FILE *fp = NULL;

    if (!(fp = fopen(CALIBRATION_LOG, "r"))) {
        ESP_LOGE(TAG, "Can't open file for logging");
        return;
    }

    while (fgets(hexbuf, sizeof (hexbuf), fp) != NULL) {
        char *end = strchr(hexbuf, '\n');
        if (end) {
            *end = 0;
        }

        ESP_LOGI(TAG, "Log: %s", hexbuf);

        publish_debug_telemetry_observation_Calibration(hexbuf);
    }

    fclose(fp);
    remove(CALIBRATION_LOG);
}

bool calibration_write_default_calibration_params(CalibrationCtx *ctx) {
    CalibrationHeader header = { 0 };

    header.i_gain[0] = floatToSn(1., 21);
    header.i_gain[1] = floatToSn(1., 21);
    header.i_gain[2] = floatToSn(1., 21);

    header.v_gain[0] = floatToSn(1., 21);
    header.v_gain[1] = floatToSn(1., 21);
    header.v_gain[2] = floatToSn(1., 21);

    header.v_offset[0] = floatToSn(0., 23);
    header.v_offset[1] = floatToSn(0., 23);
    header.v_offset[2] = floatToSn(0., 23);

    header.t_offs[0] = 0xA800;
    header.t_offs[1] = 0xA800;
    header.t_offs[2] = 0xA800;

    const char *bytes = (const char *)&header;

    const char *bytesAfterCrc = bytes + sizeof(header.crc);
    uint16_t crc = CRC16(0x17FD, (uint8_t *)bytesAfterCrc, sizeof(header) - sizeof(header.crc));
    ZEncodeUint16(crc, (uint8_t *)bytes);

    char *ptr = hexbuf;
    for (size_t i = 0; i < sizeof (header); i++) {
        ptr += sprintf(ptr, "%02X", (uint8_t)bytes[i]);
    }
    *ptr = 0;

    uint8_t errorCode = 0;
    if (MCU_SendCommandWithData(CommandMidInitCalibration, bytes, sizeof (header), &errorCode) != MsgCommandAck || errorCode != 0) {
        ESP_LOGE(TAG, "%s: Writing default calibration to MCU failed!", calibration_state_to_string(ctx));
        return false;
    }

    ESP_LOGI(TAG, "%s: Wrote default calibration!", calibration_state_to_string(ctx));
    return true;
}

bool calibration_set_mode(CalibrationCtx *ctx, CalibrationMode mode) {
    if (mode == ctx->Mode) {
        ctx->ReqMode = mode;
        return true;
    }

    ESP_LOGI(TAG, "%s: Requesting mode %s", calibration_state_to_string(ctx), calibration_mode_to_string(mode));

    if (ctx->ReqMode != mode) {
        CALLOG(ctx, "- Requesting %s", calibration_mode_to_string(mode));
    }

    ctx->ReqMode = mode;
    return false;
}

bool calibration_tick_calibrate(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress:
            if (ctx->Flags & CAL_FLAG_SKIP_CAL) {
                CAL_CSTATE(ctx) = Complete;
                return true;
            }

            if (CAL_STATE(ctx) == CalibrateCurrentOffset) {
                calibration_step_calibrate_current_offset(ctx);
            } else if (CAL_STATE(ctx) == CalibrateCurrentGain) {
                calibration_step_calibrate_current_gain(ctx);
            } else if (CAL_STATE(ctx) == CalibrateVoltageOffset) {
                calibration_step_calibrate_voltage_offset(ctx);
            } else {
                calibration_step_calibrate_voltage_gain(ctx);
            }
            break;
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

int calibration_tick_starting_init(CalibrationCtx *ctx) {
    uint32_t midStatus;

    // Disable cloud communication (exception for uploading calibration data)
    cloud_observations_disable(true);

    // Disable offline sessions as well, chargers should be online during calibration but just in case...
    offlineSession_disable();
    offlineLog_disable();

    if (!calibration_turn_led_off(ctx)) {
        return -5;
    }

    if (!calibration_get_calibration_id(ctx, &ctx->Params.CalibrationId)) {
        return -4;
    }

    if (ctx->Params.CalibrationId != 0) {
        if (CAL_STATE(ctx) == Starting) {
            ESP_LOGI(TAG, "Calibration already done (ID = %d)!", ctx->Params.CalibrationId);
        }

        ctx->Flags |= CAL_FLAG_SKIP_CAL;
        ctx->Flags |= CAL_FLAG_UPLOAD_PAR;

        if (MCU_GetMidStatus(&midStatus) && !(midStatus & MID_STATUS_NOT_VERIFIED)) {
            ESP_LOGI(TAG, "Calibration already verified (ID = %d)!", ctx->Params.CalibrationId);
            ctx->Flags |= CAL_FLAG_UPLOAD_VER;
        }
    }

    /* Should be standalone already?
    if (!calibration_set_standalone(ctx, 1)) {
        return -1;
    }
    */

    if (!calibration_set_simplified_max_current(ctx, 32.0)) {
        return -2;
    }

    if (!calibration_set_lock_cable(ctx, 0)) {
        return -3;
    }

    return 0;
}

bool calibration_tick_starting(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress: {
            if (!(ctx->Flags & CAL_FLAG_INIT)) {

                int ret = calibration_tick_starting_init(ctx);
                // Turn off LED, etc
                if (ret < 0) {
                    CALLOG(ctx, "- Starting couldn't complete - %d", ret);
                    return false;
                }

                ctx->Flags |= CAL_FLAG_INIT;
            }

            // Enter MID mode
            if (calibration_set_mode(ctx, Closed)) {
                CAL_CSTATE(ctx) = Complete;
            }

            break;
        }
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

bool calibration_tick_contact_cleaning(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress:
            // TODO: Not implemented, do we need to?
            CAL_CSTATE(ctx) = Complete;
            break;
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

bool calibration_tick_close_relays(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress: {
            if (calibration_set_mode(ctx, Closed)) {
                CAL_CSTATE(ctx) = Complete;
            }
            break;
        }
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

int calibration_phases_within(float *phases, float nominal, float range) {
    float min = nominal * (1.0 - range);
    float max = nominal * (1.0 + range);

    int count = 0;
    if (phases[0] >= min && phases[0] <= max) count++;
    if (phases[1] >= min && phases[1] <= max) count++;
    if (phases[2] >= min && phases[2] <= max) count++;

    return count;
}

bool calibration_tick_warming_up(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress: {
            uint8_t source;
            float voltage[3];
            float current[3];

            if (!calibration_set_mode(ctx, Closed)) {
                return false;
            }

            if (!calibration_get_emeter_snapshot(ctx, &source, current, voltage)) {
                break;
            }

            if (pdTICKS_TO_MS(xTaskGetTickCount() - ctx->Ticks[CURRENT_TICK]) > 2000) {
                // Ignore stale reference broadcasts... wait for more recent
                break;
            }

            float expectedCurrent = 5.0;
            float expectedVoltage = 230.0;

            float allowedCurrent = 0.1;
            float allowedVoltage = 0.1;

            TickType_t maxWait = pdMS_TO_TICKS(5 * 1000);

            if (ctx->VerTest & MediumLevelCurrent) {
                expectedCurrent = 10.0;
            } else if (ctx->VerTest & HighLevelCurrent) {
                expectedCurrent = 32.0;
            } else {
                expectedCurrent = 16.0;
            }

            if (calibration_phases_within(current, expectedCurrent, allowedCurrent) == 3
             && calibration_phases_within(voltage, expectedVoltage, allowedVoltage) == 3) {
                ctx->Ticks[WARMUP_WAIT_TICK] = 0;

                if (ctx->Ticks[WARMUP_TICK] == 0) {
                    ctx->Ticks[WARMUP_TICK] = xTaskGetTickCount();
                } else {
                    if (xTaskGetTickCount() - ctx->Ticks[WARMUP_TICK] > maxWait) {
                        ctx->Ticks[WARMUP_TICK] = 0;
                        CAL_CSTATE(ctx) = Complete;
                        break;
                    } else {
                        ESP_LOGI(TAG, "%s: Warming up (%.1fA for %ds) ...", calibration_state_to_string(ctx), expectedCurrent, pdTICKS_TO_MS(maxWait) / 1000);
                    }
                }

            } else {
                ctx->Ticks[WARMUP_TICK] = 0;

                if (ctx->Ticks[WARMUP_WAIT_TICK] == 0) {
                    ctx->Ticks[WARMUP_WAIT_TICK] = xTaskGetTickCount();
                } else if (xTaskGetTickCount() - ctx->Ticks[WARMUP_WAIT_TICK] > maxWait) {
                    ctx->Ticks[WARMUP_WAIT_TICK] = 0;
                    CAL_CSTATE(ctx) = Complete;

                    if (calibration_is_simulation()) {
                        ESP_LOGI(TAG, "Resetting warmup current");
                        extern double _test_currents[];
                        _test_currents[WarmingUp] = 32.0;
                    }

                    break;
                }

                ESP_LOGI(TAG, "%s: Waiting to be in range (%.1fA +/- %.1f%% range, I %.1fA %.1fA %.1fA) ...", calibration_state_to_string(ctx), expectedCurrent, allowedCurrent * 100.0, current[0], current[1], current[2]);
            }

            break;
        }
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

bool calibration_tick_warmup_steady_state_temp(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress: {
            if (!calibration_set_mode(ctx, Closed)) {
                return false;
            }

            float totalPower;
            if (calibration_get_total_charge_power(ctx, &totalPower)) {
                if (totalPower <= 50.0f) {
                    if (!ctx->Ticks[STABILIZATION_TICK]) {
                        ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
                    }

                    if (xTaskGetTickCount() > ctx->Ticks[STABILIZATION_TICK]) {
                        CAL_CSTATE(ctx) = Complete;
                    } else {
                        ESP_LOGI(TAG, "%s: Waiting for temperatures to even out ...", calibration_state_to_string(ctx));
                    }
                } else {
                    ctx->Ticks[STABILIZATION_TICK] = 0;
                    ESP_LOGE(TAG, "%s: Total charge power too high %.1fW > 50W!", calibration_state_to_string(ctx), totalPower);
                }
            } else {
                ESP_LOGE(TAG, "%s: Sending total charge power failed!", calibration_state_to_string(ctx));
            }

            break;
        }
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

bool calibration_tick_write_calibration_params(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    if (CAL_CSTATE(ctx) != InProgress) {
        return false;
    }

    CalibrationParameter *params[] = {
        ctx->Params.CurrentGain,
        ctx->Params.VoltageGain,
        ctx->Params.CurrentOffset,
        ctx->Params.VoltageOffset,
    };

    CalibrationHeader header;
    header.crc = 0;
    header.calibration_id = ctx->Params.CalibrationId;
    header.functional_relay_revision = 0;

    CalibrationParameter *param = ctx->Params.CurrentGain;
    header.i_gain[0] = floatToSn(param[0].value, 21);
    header.i_gain[1] = floatToSn(param[1].value, 21);
    header.i_gain[2] = floatToSn(param[2].value, 21);

    param = ctx->Params.VoltageGain;
    header.v_gain[0] = floatToSn(param[0].value, 21);
    header.v_gain[1] = floatToSn(param[1].value, 21);
    header.v_gain[2] = floatToSn(param[2].value, 21);

    param = ctx->Params.VoltageOffset;
    header.v_offset[0] = floatToSn(param[0].value, 23);
    header.v_offset[1] = floatToSn(param[1].value, 23);
    header.v_offset[2] = floatToSn(param[2].value, 23);

    header.t_offs[0] = 0xA800;
    header.t_offs[1] = 0xA800;
    header.t_offs[2] = 0xA800;

    const char *bytes = (const char *)&header;

    const char *bytesAfterCrc = bytes + sizeof(header.crc);
    uint16_t crc = CRC16(0x17FD, (uint8_t *)bytesAfterCrc, sizeof(header) - sizeof(header.crc));
    ZEncodeUint16(crc, (uint8_t *)bytes);

    char *ptr = hexbuf;
    for (size_t i = 0; i < sizeof (header); i++) {
        ptr += sprintf(ptr, "%02X", (uint8_t)bytes[i]);
    }
    *ptr = 0;

    if (!(ctx->Flags & CAL_FLAG_SKIP_CAL)) {

        // NOTE: Can ignore param 2 because current offset doesn't need to be calibrated!
        for (size_t param = 0; param < sizeof (params) / sizeof (params[0]); param++) {
            for (int phase = 0; phase < 3; phase++) {
                if (!params[param][phase].assigned) {
                    if (param != 2) {
                        calibration_fail(ctx, "No calibration value assigned (%d, L%d)!", param, phase);
                        return false;
                    }
                }
            }
        }

    }

    if (!(ctx->Flags & CAL_FLAG_UPLOAD_PAR)) {
        // Let charger go into "idle" state where we don't fail if not in MID mode
        if (!calibration_set_mode(ctx, Idle)) {
            return false;
        }

        // Upload to cloud as observation
        calibration_https_upload_to_cloud(ctx, hexbuf);

        if (calibration_https_upload_parameters(ctx, hexbuf, false)) {

            // Recompute header bytes with given calibration ID
            header.calibration_id = ctx->Params.CalibrationId;
            const char *bytesAfterCrc = bytes + sizeof(header.crc);
            uint16_t crc = CRC16(0x17FD, (uint8_t *)bytesAfterCrc, sizeof(header) - sizeof(header.crc));
            ZEncodeUint16(crc, (uint8_t *)bytes);

            ctx->Flags |= CAL_FLAG_UPLOAD_PAR;
        } else {
            if (ctx->Retries < 5) {
                ESP_LOGE(TAG, "%s: Failure to upload parameters, retrying!", calibration_state_to_string(ctx));
                ctx->Retries++;
            } else {
                calibration_fail(ctx, "Couldn't upload calibration data to production server!");
            }
            return false;
        }
    }

    ctx->Retries = 0;

    if (!(ctx->Flags & CAL_FLAG_WROTE_PARAMS)) {
        // Let charger go into "idle" state where we don't fail if not in MID mode
        if (!calibration_set_mode(ctx, Idle)) {
            return false;
        }

        if (ctx->Flags & CAL_FLAG_SKIP_CAL) {
            if (MCU_SendCommandId(CommandReset) != MsgCommandAck) {
                ESP_LOGE(TAG, "%s: Failed to reboot MCU!", calibration_state_to_string(ctx));
                return false;
            }
        } else {
            uint8_t errorCode = 0;
            if (MCU_SendCommandWithData(CommandMidInitCalibration, bytes, sizeof (header), &errorCode) != MsgCommandAck || errorCode != 0) {
                ESP_LOGE(TAG, "%s: Writing calibration to MCU failed!", calibration_state_to_string(ctx));
                return false;
            }
        }

        ctx->Flags |= CAL_FLAG_WROTE_PARAMS;
        ctx->Flags &= ~CAL_FLAG_INIT;
        ctx->Ticks[WRITE_TICK] = 0;

        return false;

    } else {
        // Rebooted, turn LED off, standalone current, etc. again!
        if (!(ctx->Flags & CAL_FLAG_INIT)) {
            int ret = calibration_tick_starting_init(ctx);
            if (ret < 0) {
                CALLOG(ctx, "- Starting couldn't complete - %d", ret);
                return false;
            }

            ctx->Flags |= CAL_FLAG_INIT;
        }

        // Give MCU ~10 seconds to reboot and current to stabilize, otherwise current transformers
        // go into overload even after relays close, requiring a manual reset.
        if (!ctx->Ticks[WRITE_TICK]) {
            ESP_LOGI(TAG, "%s: Delaying completion ...", calibration_state_to_string(ctx));
            ctx->Ticks[WRITE_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
            return false;
        }

        if (xTaskGetTickCount() < ctx->Ticks[WRITE_TICK]) {
            return false;
        }

        // Enter MID mode
        if (!calibration_set_mode(ctx, Closed)) {
            return false;
        }

        CAL_CSTATE(ctx) = Complete;

        return CAL_CSTATE(ctx) != state;
    }
}

bool calibration_tick_done(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress:
            if (!calibration_set_mode(ctx, Idle)) {
                return false;
            }

            if (!(ctx->Flags & CAL_FLAG_UPLOAD_VER)) {
                // Upload parameters
                if (!calibration_https_upload_parameters(ctx, NULL, true)) {
                    if (ctx->Retries < 5) {
                        ESP_LOGE(TAG, "%s: Failure to upload parameters, retrying!", calibration_state_to_string(ctx));
                        ctx->Retries++;
                    } else {
                        calibration_fail(ctx, "Couldn't upload calibration data to production server!");
                    }
                    return false;
                } else {
                    ctx->Flags |= CAL_FLAG_UPLOAD_VER;
                }


            } else {
                ESP_LOGI(TAG, "Already uploaded verification data, skipping!");
            }

            uint32_t calId = ctx->Params.CalibrationId;

            uint8_t idBytes[4];
            idBytes[0] = (uint8_t)(calId & 0xFF);
            idBytes[1] = (uint8_t)((calId >> 8) & 0xFF);
            idBytes[2] = (uint8_t)((calId >> 16) & 0xFF);
            idBytes[3] = (uint8_t)((calId >> 24) & 0xFF);

            uint16_t crc = CRC16(0x2917, idBytes, sizeof (idBytes));

            uint8_t crcBytes[2];
            ZEncodeUint16(crc, crcBytes);

            ESP_LOGI(TAG, "Marking calibration as verified ID %d CRC 0x%04X", calId, crc);

            uint8_t errorCode = 0;
            if (MCU_SendCommandWithData(CommandMidMarkCalibrationVerified, (const char *)crcBytes, sizeof (crcBytes), &errorCode) != MsgCommandAck || errorCode != 0) {
                calibration_fail(ctx, "Failed to mark calibration as verified!");
                return false;
            }

            CALLOG(ctx, "- Verified ID %d, CRC 0x%04X", calId, crc);

            CAL_CSTATE(ctx) = Complete;
            break;
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

void calibration_update_charger_state(CalibrationCtx *ctx) {
    uint8_t isClosed = MCU_GetRelayStates() & 1;
    uint8_t isOpen = !isClosed;

    static TickType_t lastRefresh, midModeStart;

    if (ctx->Mode == Idle) {
       if (ctx->ReqMode != Idle) {
            // Enter MID Mode
            bool midModeActive = calibration_refresh(ctx);
            bool calibrationActive = calibration_is_active(ctx);

            if (!midModeActive || !calibrationActive) {
                ESP_LOGI(TAG, "%s: Trying to enter MID mode!", calibration_state_to_string(ctx));
                calibration_start_mid_mode(ctx);
                lastRefresh = midModeStart = xTaskGetTickCount();
                return;
            } else {
                ESP_LOGI(TAG, "%s: Entered MID mode!", calibration_state_to_string(ctx));
            }
       } else {
           // Idle mode, stop any charging
           calibration_open_relays(ctx);
           return;
       }
    }

    bool midModeActive = calibration_refresh(ctx);
    TickType_t refreshTime = xTaskGetTickCount();

    uint32_t msSinceRefresh = pdTICKS_TO_MS(refreshTime - lastRefresh);
    uint32_t msSinceStart = pdTICKS_TO_MS(refreshTime - midModeStart);
    
    if (msSinceRefresh > 3000) {
        ESP_LOGI(TAG, "%s: MID mode refresh near threshold (%dms, %dms since start)!", calibration_state_to_string(ctx), msSinceRefresh, msSinceStart);
    }

    if (!midModeActive) {
        calibration_fail(ctx, "Unexpectedly exited MID mode (%dms since last refresh, %dms since start)!", msSinceRefresh, msSinceStart);
    }

    lastRefresh = refreshTime;

    if (ctx->Mode == ctx->ReqMode) {

        if (ctx->Mode == Open) {
            if (isClosed) {
                calibration_fail(ctx, "Relay closed when it should be open!");
            }
        } else {
            if (isOpen) {
                calibration_fail(ctx, "Relay open when it should be closed!");
            }
        }

    } else {

        if (ctx->ReqMode == Open || ctx->ReqMode == Idle) {
            if (isOpen) {
                ESP_LOGI(TAG, "%s: Relay opened...", calibration_state_to_string(ctx));
                ctx->Mode = ctx->ReqMode;
            } else {
                ESP_LOGI(TAG, "%s: Requesting relay open...", calibration_state_to_string(ctx));
                calibration_open_relays(ctx);
            }
        } else {

            if (isClosed) {
                ESP_LOGI(TAG, "%s: Relay closed...", calibration_state_to_string(ctx));
                ctx->Mode = ctx->ReqMode;
            } else {
                ESP_LOGI(TAG, "%s: Requesting relay closed...", calibration_state_to_string(ctx));

                if (MCU_GetChargeOperatingMode() == CHARGE_OPERATION_STATE_PAUSED) {
                    ESP_LOGI(TAG, "%s: Was in paused mode, final stop!", calibration_state_to_string(ctx));
                    calibration_open_relays(ctx);
                }

                calibration_close_relays(ctx);
            }
        }
    }
}

void calibration_mode_test(CalibrationCtx *ctx) {
    while (true) {
        uint32_t i = esp_random() % 3;

        if (i == ctx->Mode) {
            continue;
        }

        char *fromState = ctx->Mode == 0 ? "Idle" : ctx->Mode == 1 ? "Open" : "Closed";
        char *toState = i == 0 ? "Idle" : i == 1 ? "Open" : "Closed";
        ESP_LOGI(TAG, "Testing %s -> %s", fromState, toState);

        while (!calibration_set_mode(ctx, i)) {
            vTaskDelay(pdMS_TO_TICKS(500));
            calibration_update_charger_state(ctx);
        }

        int times = 10;

        ESP_LOGI(TAG, "In %s, waiting for %d sec", toState, times / 2);
        for (int i = 0; i < times; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            calibration_update_charger_state(ctx);
        }
    }
}

int calibration_send_state(CalibrationCtx *ctx) {
    devInfo = i2cGetLoadedDeviceInfo();

    ChargerStateUdpMessage reply = ChargerStateUdpMessage_init_zero;

    reply.Serial = devInfo.serialNumber;
    reply.State = CAL_CSTATE(ctx);
    reply.StateAck = CAL_STATE(ctx);
    reply.SequenceAck = ctx->Seq;
    reply.RunAck = ctx->Run;
    reply.OverloadedPhases = ctx->Overloaded;
    reply.has_Status = ctx->FailReason != NULL;
    reply.Status.Status = (char *)ctx->FailReason;

    if (CAL_STATE(ctx) == Starting) {
        reply.has_Init = true;

        reply.Init.ClientProtocol = 2;
        reply.Init.FirmwareVersion = GetSoftwareVersion();
        reply.Init.Uptime = 0;

        // TODO: Based on version and/or hardware revision?
        reply.Init.NeedsMIDCalibration = true;

        reply.Init.HasProductionTestPassed = devInfo.factory_stage == FactoryStageFinnished;

        uint32_t midStatus;
        MCU_GetMidStatus(&midStatus);

        uint32_t calId;
        MCU_GetMidStoredCalibrationId(&calId);

        reply.Init.IsCalibrated = calId != 0;
        reply.Init.IsVerified = (calId != 0) && !(midStatus & MID_STATUS_NOT_VERIFIED);

        reply.Init.Has4G = true;
        reply.Init.Is4GVerified = true;
    }

    pb_ostream_t strm = pb_ostream_from_buffer((uint8_t *)buf, sizeof (buf));

    if (!pb_encode(&strm, ChargerStateUdpMessage_fields, &reply)) {
        ESP_LOGE(TAG, "Encoding charger state message failed: %s", PB_GET_ERROR(&strm));
        return 0;
    }

    int err = sendto(ctx->Server.Socket, buf, strm.bytes_written, 0, (struct sockaddr *)&ctx->Server.ServAddr, sizeof (ctx->Server.ServAddr));
    if (err < 0) {
        ESP_LOGE(TAG, "Sending charger state failed: %d", errno);
        return 0;
    } else {
        ctx->PktsSent++;
    }

    return ctx->Seq++;
}

static bool isCalibratedFlag = false;
bool calibration_get_finished_flag()
{
	return isCalibratedFlag;
}

void calibration_finish(CalibrationCtx *ctx, bool failed) {
    if (!calibration_set_mode(ctx, Idle)) {
        ESP_LOGI(TAG, "Waiting for charger to go idle...");
        return;
    }

    if (!(ctx->Flags & CAL_FLAG_DONE)) {
        // Allow sending observations again so capabilities and log can be uploaded...
        cloud_observations_disable(false);

        if (failed) {
            ESP_LOGE(TAG, "%s: Calibration failed!", calibration_state_to_string(ctx));

            // If failed, then maybe got a bad calibration, allow rewriting?
            if (MCU_SendCommandId(CommandMidClearCalibration) != MsgCommandAck) {
                ESP_LOGE(TAG, "%s: Couldn't clear calibration!", calibration_state_to_string(ctx));
                return;
            }
        } else {
            ESP_LOGI(TAG, "%s: Calibration complete!", calibration_state_to_string(ctx));
            // Re-enable cloud publishing so capabilities can be sent
            cloud_observations_disable(false);
            isCalibratedFlag = true;
        }

        calibration_log_dump();

        ctx->Flags |= CAL_FLAG_DONE;
    }

    calibration_turn_led_off(ctx);

    static int blinkDelay = 0;

    if (blinkDelay % 2 == 0) {
        // Indicate PASS/FAIL with by blinking green/red every tick
        if (failed) {
            ESP_LOGE(TAG, "Blink!");
            calibration_blink_led_red(ctx);
        } else {
            ESP_LOGI(TAG, "Blink!");
            calibration_blink_led_green(ctx);
        }
    }

    blinkDelay++;

    calibration_send_state(ctx);
}

void calibration_handle_tick(CalibrationCtx *ctx) {
    TickType_t curTick = xTaskGetTickCount();

    CalibrationMode mode_before = ctx->Mode;
    calibration_update_charger_state(ctx);
    CalibrationMode mode_after = ctx->Mode;

    if (mode_before != mode_after) {
        CALLOG(ctx, "- %s -> %s", calibration_mode_to_string(mode_before), calibration_mode_to_string(mode_after));
    }

    // No tick within states if done or failed, but allow above to execute so we send
    // state back to the app
    if (CAL_STATE(ctx) == Done && CAL_CSTATE(ctx) == Complete) {
        calibration_finish(ctx, false);
        return;
    }

    if (CAL_CSTATE(ctx) == Failed) {
        calibration_finish(ctx, true);
        return;
    }

    // Need to have received message from server in last 20 seconds, otherwise fail
    if (pdTICKS_TO_MS(curTick - ctx->Ticks[ALIVE_TICK]) > 20000) {
        calibration_fail(ctx, "No message from server in last 20 seconds");
        return;
    }

    bool verPass = false;

    uint32_t speedVer = MCU_GetHwIdMCUSpeed();
    switch (speedVer) {
        case 5:
        case 7:
            verPass = true;
            break;
        default:
            break;
    }

    if (!verPass) {
        calibration_fail(ctx, "Can only calibrate MID/EU Speed boards, detected Speed v%d", speedVer);
        return;
    }

    uint32_t allowedMidBits = MID_STATUS_ALL_PAGES_EMPTY | MID_STATUS_NOT_CALIBRATED | MID_STATUS_NOT_VERIFIED | MID_STATUS_NOT_INITIALIZED | MID_STATUS_TICK_TIMEOUT;
    switch(CAL_STATE(ctx)) {
        case VerificationStart:
        case VerificationRunning:
        case VerificationDone:
        case Done:
            // Any other bits here would not allow verification
            allowedMidBits = MID_STATUS_NOT_VERIFIED;
            break;
        default:
            break;
    }

    uint32_t status;
    if (calibration_read_mid_status(&status)) {
        status &= ~allowedMidBits;

        if (status) {
            calibration_fail(ctx, "Unexpected MID status 0x%08X", status);
        }
    }
    
    uint32_t warnings;
    if (calibration_read_warnings(&warnings)) {
        warnings &= ~WARNING_PILOT_NO_PROXIMITY;
        warnings &= ~WARNING_NO_SWITCH_POW_DEF;
        // RCD triggers on boot sometimes?
        warnings &= ~WARNING_RCD;
        warnings &= ~WARNING_MID;
        if (warnings) {
            // Warning set so relays probably can't be controlled anyway, so fail
            calibration_fail(ctx, "Unexpected MCU warning 0x%08X", warnings);
            return;
        }
    }

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[STATE_TICK]) > CONFIG_CAL_TIMEOUT) {
        calibration_send_state(ctx);
        ctx->Ticks[STATE_TICK] = curTick;
    }

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[TICK]) < CONFIG_CAL_TIMEOUT) {
        return;
    }

    bool updated = false;

    static bool first_call = true;

    CalibrationState state = CAL_STATE(ctx);
    CalibrationChargerState cstate = CAL_CSTATE(ctx);

    //
    // Order of steps from App:
    //
    // 1. Starting - ensure relays open, initialize MCU settings
    // 2. CalibrateCurrentOffset - possibly not needed can just use HPF_COEF_I, but for verification?
    // 3. CloseRelays - ensure relays closed
    // 4. WarmingUp
    // 5. WarmupSteadyStateTemp 
    // 6. CalibrateVoltageOffset
    // 7. CalibrateVoltageGain
    // 8. CalibrateCurrentGain
    // 9. WriteCalibrationParameters
    // 10. VerificationStart
    // 11. VerificationRunning
    // 12. VerificationDone
    //

    switch(CAL_STATE(ctx)) {
        case Starting:
            updated = calibration_tick_starting(ctx);
            break;
        case ContactCleaning:
            updated = calibration_tick_contact_cleaning(ctx);
            break;
        case WarmingUp:
            updated = calibration_tick_warming_up(ctx);
            break;
        case WarmupSteadyStateTemp:
            updated = calibration_tick_warmup_steady_state_temp(ctx);
            break;
        case CalibrateCurrentOffset:
        case CalibrateVoltageOffset:
        case CalibrateVoltageGain:
        case CalibrateCurrentGain:
            updated = calibration_tick_calibrate(ctx);
            break;
        case VerificationStart:
        case VerificationRunning:
        case VerificationDone:
            updated = calibration_tick_verification(ctx);
            break;
        case WriteCalibrationParameters:
            updated = calibration_tick_write_calibration_params(ctx);
            break;
        case CloseRelays:
            updated = calibration_tick_close_relays(ctx);
            break;
        case Done:
            updated = calibration_tick_done(ctx);
            break;
    }

    CalibrationState state_after = CAL_STATE(ctx);
    CalibrationChargerState cstate_after = CAL_CSTATE(ctx);

    if (state != state_after || cstate != cstate_after || first_call) {
        CALLOG(ctx, "");
    }

    if (updated) {
        ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx), charger_state_to_string(ctx));
    }

    ctx->Ticks[TICK] = xTaskGetTickCount();

    first_call = false;
}

void calibration_handle_ack(CalibrationCtx *ctx, CalibrationUdpMessage_ChargerAck *msg) {
    /* ESP_LOGI(TAG, "ChargerAck { %d }", msg->Sequence); */
    ctx->PktsAckd++;
}

void calibration_handle_data(CalibrationCtx *ctx, CalibrationUdpMessage_DataMessage *msg) {
    bool hasCurrent = false;

    if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterVoltage_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterVoltage;
        ctx->Ref.V[0] = phases.L1;
        ctx->Ref.V[1] = phases.L2;
        ctx->Ref.V[2] = phases.L3;
        ctx->Ticks[VOLTAGE_TICK] = xTaskGetTickCount();
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterCurrent_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterCurrent;
        ctx->Ref.I[0] = phases.L1;
        ctx->Ref.I[1] = phases.L2;
        ctx->Ref.I[2] = phases.L3;
        ctx->Ticks[CURRENT_TICK] = xTaskGetTickCount();

        hasCurrent = true;
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterEnergy_tag) {
        CalibrationUdpMessage_DataMessage_EnergySnapshot energy = msg->message_type.ReferenceMeterEnergy;
        ctx->Ref.E = energy.WattHours;
        ctx->Ticks[ENERGY_TICK] = xTaskGetTickCount();
    } else {
        ESP_LOGE(TAG, "Unknown CalibrationUdpMessage.Data type!");
    }

    CalibrationOverload overloadBefore = ctx->Overloaded;

    // Do simplified overload checking
    ctx->Overloaded = None;

    float localCurrents[3];
    if (!calibration_get_current_snapshot(ctx, localCurrents)) {
        return;
    }
   
    if (ctx->Ref.OverloadIsEstimated) {
        for (int i = 0; i < 3; i++) {
            if (ctx->Ref.OverloadPhases & (1 << i)) {
                float localCurrent = localCurrents[i];
                float remoteCurrent = ctx->Ref.OverloadCurrent;
                if (remoteCurrent > 0.35 && localCurrent < (remoteCurrent * 0.5)) {
                    ctx->Overloaded |= (1 << i);
                }
            }
        }
    }

    if (hasCurrent) {
        for (int i = 0; i < 3; i++) {
            float remoteCurrent = ctx->Ref.I[i];
            float localCurrent = localCurrents[i];

            if (remoteCurrent > 0.35 && localCurrent < (remoteCurrent * 0.5)) {
                ctx->Overloaded |= (1 << i);
            }
        }
    }

    if (overloadBefore != ctx->Overloaded) {
        CALLOG(ctx, "- Overload %d%d%d %.2f %.2f %.2f",
                !!(ctx->Overloaded & (1 << 0)),
                !!(ctx->Overloaded & (1 << 1)),
                !!(ctx->Overloaded & (1 << 2)),
                localCurrents[0], localCurrents[1], localCurrents[2]);
    }
}

void calibration_reset(CalibrationCtx *ctx) {
    // Keep connection to server, clear the rest
    CalibrationServer server = ctx->Server;
    memset(ctx, 0, sizeof (*ctx));
    ctx->Server = server;
    ctx->Position = -1;
    CAL_STATE(ctx) = Starting;
    CAL_CSTATE(ctx) = InProgress;
    CAL_STEP(ctx) = InitRelays;
}

void calibration_handle_state(CalibrationCtx *ctx, CalibrationUdpMessage_StateMessage *msg) {
    bool isRun = msg->State == Starting && msg->has_Run;

    // If failed, allow starting a new run
    if (!isRun && CAL_CSTATE(ctx) == Failed) {
        return;
    }

    if (isRun) {
        int doCalibRun = 0;
        int testPos = -1;

        for (pb_size_t i = 0; i < msg->Run.SelectedSerials_count; i++) {
            char *serial = msg->Run.SelectedSerials[i];
            if (strcmp(serial, devInfo.serialNumber) == 0) {
                doCalibRun = 1;
                testPos = i;
                break;
            }
        }

        if (!doCalibRun) {
            ESP_LOGE(TAG, "Invalid serial!");
        }

        if (strcmp(msg->Run.Key, CONFIG_CAL_KEY) != 0) {
            ESP_LOGE(TAG, "Invalid key!");
            doCalibRun = 0;
        }

        if (!doCalibRun) {
            ESP_LOGE(TAG, "Got run command from server, but was not validated!");
            return;
        } else if (ctx->Run != msg->Run.Run) {
            calibration_reset(ctx);
            ctx->Run = msg->Run.Run;
            ctx->Position = testPos;
            ESP_LOGI(TAG, "Starting run %d in position %d", ctx->Run, ctx->Position);
        }

        if (strcmp(msg->Run.SetupName, "dev") == 0) {
            ESP_LOGI(TAG, "Run is a simulation, uploading disabled!");
            //ctx->Flags |= CAL_FLAG_UPLOAD_PAR | CAL_FLAG_UPLOAD_VER;
            calibration_set_simulation(true);
        } else {
            calibration_set_simulation(false);
        }
    }

    if (msg->Sequence < ctx->LastSeq) {
        ESP_LOGE(TAG, "Received packet out of sequence! %d <= %d, Ignoring...", msg->Sequence, ctx->LastSeq);
        return;
    }

    if (CAL_STATE(ctx) != msg->State) {
        /* ESP_LOGI(TAG, "%s", calibration_state_to_string(msg->State)); */
    } else {
        return;
    }

    if (msg->has_Verification) {
        ESP_LOGI(TAG, "%s: Test ID %d", calibration_state_to_string(ctx), msg->Verification.TestId);
        ctx->VerTest = msg->Verification.TestId;
    }

    ctx->Flags &= ~CAL_FLAG_INIT;

    // Reset state for next ticks
    CAL_STATE(ctx) = msg->State;
    CAL_CSTATE(ctx) = InProgress;
    CAL_STEP(ctx) = InitRelays;
}

static int socket_add_ipv4_multicast_group(int sock, bool assign_source_if) {
    struct ip_mreq imreq = { 0 };
    struct in_addr iaddr = { 0 };
    int err = 0;
    // Configure source interface
#if 1
    imreq.imr_interface.s_addr = IPADDR_ANY;
#else
    esp_netif_ip_info_t ip_info = { 0 };
    err = esp_netif_get_ip_info(get_example_netif(), &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", err);
        goto err;
    }
    inet_addr_from_ip4addr(&iaddr, &ip_info.ip);
#endif // LISTEN_ALL_IF
    // Configure multicast address to listen to
    err = inet_aton(CONFIG_CAL_SERVER_IP, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", CONFIG_CAL_SERVER_IP);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
        ESP_LOGW(TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", CONFIG_CAL_SERVER_IP);
    }

    if (assign_source_if) {
        // Assign the IPv4 multicast source interface, via its IP
        // (only necessary if this socket is IPV4 only)
        err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                         sizeof(struct in_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF: %d", errno);
            goto err;
        }
    }

    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         &imreq, sizeof(struct ip_mreq));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP: %d", errno);
        goto err;
    }

 err:
    return err;
}

static int create_multicast_ipv4_socket(void) {
    struct sockaddr_in saddr = { 0 };
    int sock = -1;
    int err = 0;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return -1;
    }

    // Bind the socket to any address
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(CONFIG_CAL_SERVER_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        goto err;
    }


    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL: %d", errno);
        goto err;
    }

    // this is also a listening socket, so add it to the multicast
    // group for listening...
    err = socket_add_ipv4_multicast_group(sock, true);
    if (err < 0) {
        goto err;
    }

    // All set, socket is configured for sending and receiving
    return sock;

err:
    close(sock);
    return -1;
}

void calibration_debug_udp_message(CalibrationUdpMessage *msg) {
    ESP_LOGI(TAG, "Message: ");

    if (msg->has_Ack) {
        printf("Ack { Sequence: %d }\n", msg->Ack.Sequence);
    }
    if (msg->has_Data) {
        if (msg->Data.which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterVoltage_tag) {
            CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->Data.message_type.ReferenceMeterVoltage;
            printf("Data { Voltage: [%f, %f, %f] }\n", phases.L1, phases.L2, phases.L3);
        } else if (msg->Data.which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterCurrent_tag) {
            CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->Data.message_type.ReferenceMeterCurrent;
            printf("Data { Current: [%f, %f, %f] }\n", phases.L1, phases.L2, phases.L3);
        } else {
            CalibrationUdpMessage_DataMessage_EnergySnapshot energy = msg->Data.message_type.ReferenceMeterEnergy;
            printf("Data { Energy: %f }\n", energy.WattHours);
        }
    }
    if (msg->has_State) {
        printf("State { %d, Sequence: %d", msg->State.State, msg->State.Sequence);

        if (msg->State.has_Run) {
            printf(" Run { Run: %d Key: %s Protocol: %d Serials: [", msg->State.Run.Run, msg->State.Run.Key, msg->State.Run.ServerProtocol);

            for (int i = 0; i < msg->State.Run.SelectedSerials_count; i++) {
                if (i != 0) printf(", ");
                printf("%s", msg->State.Run.SelectedSerials[i]);
            }

            printf("] }");
        }

        if (msg->State.has_Verification) {
            printf(" Verification { TestId: %d }", msg->State.Verification.TestId);
        }

        printf(" }\n");
    }
}

void calibration_task(void *pvParameters) {
    devInfo = i2cGetLoadedDeviceInfo();

    //calibration_mode_test(&ctx);

    while (1) {
        if (connectivity_GetActivateInterface() != eCONNECTION_WIFI) {
            ESP_LOGI(TAG, "Activating WiFi interface ...");
            connectivity_ActivateInterface(eCONNECTION_WIFI);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!network_WifiIsConnected()) {
            ESP_LOGI(TAG, "Waiting for WiFi connection ...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int sock = create_multicast_ipv4_socket();
        if (sock < 0) {
            ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "UDP socket created ...");

        serv.Socket = sock;
        bzero(&serv.ServAddr, sizeof (serv.ServAddr));
        serv.ServAddr.sin_port = htons(2020);
        serv.ServAddr.sin_family = PF_INET;
        serv.Initialized = false;

        ctx.Server = serv;
        ctx.Ticks[STATE_TICK] = xTaskGetTickCount();

        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(CONFIG_CAL_SERVER_PORT),
        };

        inet_aton(CONFIG_CAL_SERVER_IP, &sdestv4.sin_addr.s_addr);

        TickType_t lastTick = 0;

        int err = 1;
        while (err > 0) {
            struct timeval tv = {
                .tv_sec = 0,
                .tv_usec = 10 * 1000, // 10ms timeout
            };

            FD_ZERO(&fds);
            FD_SET(sock, &fds);

            int s = select(sock + 1, &fds, NULL, NULL, &tv);

            if (uxTaskGetStackHighWaterMark(NULL) < 128) {
                ESP_LOGE(TAG, "Calibration task close to overflowing stack!");
            }

            if (s < 0) {
                ESP_LOGE(TAG, "Select failed: %d", errno);
                err = -1;
                break;
            } else if (s > 0) {
                if (FD_ISSET(sock, &fds)) {
                    // Incoming datagram received
                    struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
                    socklen_t socklen = sizeof(raddr);
                    int len = recvfrom(sock, recvbuf, sizeof(recvbuf)-1, 0,
                                       (struct sockaddr *)&raddr, &socklen);
                    if (len < 0) {
                        ESP_LOGE(TAG, "Multicast recvfrom failed: %d", errno);
                        err = -1;
                        break;
                    }

                    // Get the sender's address as a string
                    if (raddr.ss_family == PF_INET) {
                        inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr,
                                    raddr_name, sizeof(raddr_name)-1);
                    }

                    pb_istream_t strm = pb_istream_from_buffer((uint8_t *)recvbuf, len);

                    if (!pb_decode(&strm, CalibrationUdpMessage_fields, &msg)) {
                        ESP_LOGE(TAG, "Decoding calibration message failed: %s", PB_GET_ERROR(&strm));
                        continue;
                    }

                    ctx.Server.ServAddr.sin_addr.s_addr = ((struct sockaddr_in *)&raddr)->sin_addr.s_addr;
                    ctx.Server.Initialized = true;

                    //calibration_debug_udp_message(&msg);

                    if (msg.has_Ack) {
                        calibration_handle_ack(&ctx, &msg.Ack);
                    } 

                    if (msg.has_Data) {
                        calibration_handle_data(&ctx, &msg.Data);
                    } 

                    if (msg.has_State) {
                        calibration_handle_state(&ctx, &msg.State);
                        ctx.LastSeq = msg.State.Sequence;
                    }

                    static TickType_t periodicLog = 0;

                    if (xTaskGetTickCount() - periodicLog > pdMS_TO_TICKS(30000)) {
                        int8_t rssi = 0;
                        wifi_ap_record_t wifidata;
			                  if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
                            rssi = wifidata.rssi;
                        }

                        // Every 30 seconds update log with some state
                        CALLOGTIME(&ctx, "- RSSI %d - (%d TX / %d RX)", rssi, ctx.PktsSent, ctx.PktsAckd);

                        periodicLog = xTaskGetTickCount();
                    }

                    ctx.Ticks[ALIVE_TICK] = xTaskGetTickCount();

                    // Enabled use of malloc in nanopb, so free any dynamically allocated fields...
                    pb_release(CalibrationUdpMessage_fields, &msg);
                }
            } else {
                if (!ctx.Server.Initialized) {
                    /* ESP_LOGI(TAG, "Waiting for server broadcast ..."); */
                    continue;
                }

                TickType_t tick = xTaskGetTickCount();
                if (pdTICKS_TO_MS(tick - lastTick) > 1000) {
                    calibration_handle_tick(&ctx);
                    lastTick = tick;
                }
            }
        }

        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, 0);
        close(sock);
    }
}

void calibration_task_start(void) {
    if (handle) {
        ESP_LOGE(TAG, "Calibration task already started!");
        return;
    }

    xTaskCreate(calibration_task, "calibration_task", 1024 * 5, NULL, 8, &handle);
}

void calibration_task_stop(void) {
    if (handle) {
        ESP_LOGE(TAG, "Killing calibration task!");

        vTaskDelete(handle);
        handle = NULL;
    }
}

int calibration_task_watermark(void) {
    if (handle) {
        return uxTaskGetStackHighWaterMark(handle);
    }
    return -1;
}
