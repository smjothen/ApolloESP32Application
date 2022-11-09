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
#include "sessionHandler.h"
#include "zaptec_protocol_serialisation.h"

#include "calibration_crc.h"
#include "calibration_emeter.h"

#include <calibration-message.pb.h>
#include <calibration.h>
#include <calibration_util.h>

#include <pb_decode.h>
#include <pb_encode.h>

static const char *TAG = "CALIBRATION    ";

#define CALIBRATION_STACK_SIZE 8192

StaticTask_t task_buffer;
//StackType_t *stack = NULL;
//StackType_t stack[CALIBRATION_STACK_SIZE];
TaskHandle_t handle = NULL;

// Moved out of functions to save stack space
char raddr_name[32] = { 0 };
char buf[128] = { 0 };
char recvbuf[128] = { 0 };
char hexbuf[256] = { 0 };

CalibrationCtx ctx = { 0 };
CalibrationServer serv = { 0 };
CalibrationUdpMessage msg = CalibrationUdpMessage_init_zero;

fd_set fds = { 0 };

struct DeviceInfo devInfo;

bool calibration_tick_calibrate(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress:
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

    if (!calibration_set_led_blue(ctx)) {
        return -5;
    }

    if (!calibration_set_standalone(ctx, 1)) {
        return -1;
    }

    if (!calibration_set_simplified_max_current(ctx, 32.0)) {
        return -2;
    }

    if (!calibration_set_lock_cable(ctx, 0)) {
        return -3;
    }

    if (!calibration_get_calibration_id(ctx, &ctx->Params.CalibrationId)) {
        return -4;
    }

    return 0;
}

bool calibration_tick_starting(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress: {

            if (!(ctx->Flags & CAL_FLAG_INIT)) {
                if (calibration_tick_starting_init(ctx) < 0) {
                    return false;
                }

                ctx->Flags |= CAL_FLAG_INIT;
            } else {

                if (ctx->Flags & CAL_FLAG_RELAY_CLOSED) {
                    CAL_CSTATE(ctx) = Complete;
                } else {
                    calibration_close_relays(ctx);
                    ESP_LOGI(TAG, "%s: Waiting for relays to close...", calibration_state_to_string(ctx));
                }
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
            if (!(ctx->Flags & CAL_FLAG_INIT)) {
                calibration_close_relays(ctx);
                ctx->Flags |= CAL_FLAG_INIT;
            } else {

                if (ctx->Flags & CAL_FLAG_RELAY_CLOSED) {
                    CAL_CSTATE(ctx) = Complete;
                } else {
                    calibration_close_relays(ctx);
                    ESP_LOGI(TAG, "%s: Waiting for relays to close ...", calibration_state_to_string(ctx));
                }
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
#ifdef CALIBRATION_SIMULATION
    return 3;
#endif
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

            if (!calibration_close_relays(ctx)) {
                ESP_LOGI(TAG, "%s: Waiting for relays to close!", calibration_state_to_string(ctx));
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

            TickType_t minimumDuration;
            TickType_t maxWait = pdMS_TO_TICKS(10 * 1000);

            if (ctx->VerTest & MediumLevelCurrent) {
                expectedCurrent = 10.0;
                minimumDuration = pdMS_TO_TICKS(5 * 1000);
            } else if (ctx->VerTest & HighLevelCurrent) {
                expectedCurrent = 32.0;
                minimumDuration = pdMS_TO_TICKS(5 * 1000);
            } else {
                expectedCurrent = 16.0;
                minimumDuration = pdMS_TO_TICKS(30 * 1000);
            }



            if (calibration_phases_within(current, expectedCurrent, allowedCurrent) == 3
             && calibration_phases_within(voltage, expectedVoltage, allowedVoltage) == 3) {

                if (ctx->Ticks[WARMUP_TICK] == 0) {
                    ctx->Ticks[WARMUP_TICK] = xTaskGetTickCount();
                } else {
                    if (xTaskGetTickCount() - ctx->Ticks[WARMUP_TICK] > minimumDuration) {
                        CAL_CSTATE(ctx) = Complete;
                        break;
                    } else {
                        ESP_LOGI(TAG, "%s: Warming up (%.1fA for %ds) ...", calibration_state_to_string(ctx), expectedCurrent, pdTICKS_TO_MS(minimumDuration) / 1000);
                    }
                }

            } else {
                ctx->Ticks[WARMUP_TICK] = 0;

                if (ctx->Ticks[WARMUP_WAIT_TICK] == 0) {
                    ctx->Ticks[WARMUP_WAIT_TICK] = xTaskGetTickCount();
                } else if (xTaskGetTickCount() - ctx->Ticks[WARMUP_WAIT_TICK] > maxWait) {
                    ESP_LOGE(TAG, "Warm up current/voltage out of range too long!");
                    CAL_CSTATE(ctx) = Failed;
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
            if (!calibration_close_relays(ctx)) {
                ESP_LOGI(TAG, "%s: Waiting for relays to close!", calibration_state_to_string(ctx));
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

    if (!(ctx->Flags & CAL_FLAG_WROTE_PARAMS)) {

        CalibrationParameter *params[] = {
            ctx->Params.CurrentGain,
            ctx->Params.VoltageGain,
            ctx->Params.CurrentOffset,
            ctx->Params.VoltageOffset,
        };

        for (size_t param = 0; param < sizeof (params) / sizeof (params[0]); param++) {
            for (int phase = 0; phase < 3; phase++) {
                if (!params[param][phase].assigned) {
                    ESP_LOGE(TAG, "%s: Didn't get a calibrated value (%d, L%d)!", calibration_state_to_string(ctx), param, phase);
                    return false;
                }
            }
        }

        CalibrationHeader header;
        header.crc = 0;
        //header.calibration_id = 1;
        header.calibration_id = 0;
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

        ESP_LOGI(TAG, "%s: Writing checksum: %04X", calibration_state_to_string(ctx), header.crc);

        char *ptr = hexbuf;
        for (size_t i = 0; i < sizeof (header); i++) {
            ptr += sprintf(ptr, "%02X ", (uint8_t)bytes[i]);
        }
        *ptr = 0;

        ESP_LOGI(TAG, "%s: Writing bytes: %s", calibration_state_to_string(ctx), hexbuf);

        // Let charger go into "idle" state where we don't fail if not in MID mode
        ctx->Flags |= CAL_FLAG_IDLE;

        if (MCU_SendCommandWithData(CommandMidInitCalibration, bytes, sizeof (header)) != MsgCommandAck) {
            ESP_LOGE(TAG, "%s: Writing calibration to MCU failed!", calibration_state_to_string(ctx));
            CAL_CFAIL(ctx, 0);
            return false;
        }

        ctx->Flags |= CAL_FLAG_WROTE_PARAMS;
        ctx->Ticks[WRITE_TICK] = 0;

        return false;
    } else {

        if (!calibration_is_active(ctx)) {
            ESP_LOGI(TAG, "%s: Waiting to enter MID mode ...", calibration_state_to_string(ctx));
            return false;
        }

        // Give MCU ~10 seconds to reboot and curren to stabilize, otherwise current transformers
        // go into overload even after relays close, requiring a manual reset.
        if (!ctx->Ticks[WRITE_TICK]) {
            ctx->Ticks[WRITE_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
            return false;
        }

        if (xTaskGetTickCount() < ctx->Ticks[WRITE_TICK]) {
            ESP_LOGI(TAG, "%s: Delaying completion ...", calibration_state_to_string(ctx));
            return false;
        }

        // Out of idle state, must be in MID mode!
        ctx->Flags &= ~CAL_FLAG_IDLE;

        // Rebooted, set standalone current, etc again
        calibration_tick_starting_init(ctx);

        CAL_CSTATE(ctx) = Complete;

        return CAL_CSTATE(ctx) != state;

    }
}

bool calibration_tick_done(CalibrationCtx *ctx) {
    CalibrationChargerState state = CAL_CSTATE(ctx);

    switch (CAL_CSTATE(ctx)) {
        case InProgress:
            if (!calibration_open_relays(ctx)) {
                return false;
            }

            // Init stage sets some parameters which get stored to flash on MCU so we should
            // do a factory reset...?
            if (MCU_SendUint8Parameter(CommandFactoryReset, 0) != MsgWriteAck) {
                // Should this command be a MsgCommand instead of MsgWrite?
                return false;
            }

            // TODO: 
            // 1. Mark calibration parameters as verified
            // 2. Exit MID mode

            CAL_CSTATE(ctx) = Complete;
            break;
        case Complete:
        case Failed:
            break;
    }

    return CAL_CSTATE(ctx) != state;
}

void calibration_update_charger_state(CalibrationCtx *ctx) {
    switch (MCU_GetChargeOperatingMode()) {
        case CHARGE_OPERATION_STATE_CHARGING:
            if (!(ctx->Flags & CAL_FLAG_RELAY_CLOSED)) {
                ESP_LOGI(TAG, "%s: Relays closed!", calibration_state_to_string(ctx));
            }
            ctx->Flags |= CAL_FLAG_RELAY_CLOSED;
            break;
        default:
            if (ctx->Flags & CAL_FLAG_RELAY_CLOSED) {
                ESP_LOGI(TAG, "%s: Relays open!", calibration_state_to_string(ctx));
            }
            ctx->Flags &= ~CAL_FLAG_RELAY_CLOSED;
            break;
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

    // TODO: Init reply.Init?
    pb_ostream_t strm = pb_ostream_from_buffer((uint8_t *)buf, sizeof (buf));

    if (!pb_encode(&strm, ChargerStateUdpMessage_fields, &reply)) {
        ESP_LOGE(TAG, "Encoding charger state message failed: %s", PB_GET_ERROR(&strm));
        return 0;
    }

    int err = sendto(ctx->Server.Socket, buf, strm.bytes_written, 0, (struct sockaddr *)&ctx->Server.ServAddr, sizeof (ctx->Server.ServAddr));
    if (err < 0) {
        ESP_LOGE(TAG, "Sending charger state failed: %d", errno);
        return 0;
    }

    /* ESP_LOGI(TAG, */
    /*         "State { %d, %s, %s, %s }", */
    /*         reply.SequenceAck, */
    /*         calibration_state_to_string(ctx), */
    /*         calibration_step_to_string(CAL_STEP(ctx)), */
    /*         charger_state_to_string(CAL_CSTATE(ctx))); */

    return ctx->Seq++;
}

void calibration_handle_tick(CalibrationCtx *ctx) {
    TickType_t curTick = xTaskGetTickCount();

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[STATE_TICK]) > CALIBRATION_TIMEOUT) {
        bool midModeActive = calibration_refresh(ctx);
        bool calibrationActive = calibration_is_active(ctx);

        if (!midModeActive || !calibrationActive) {
            ESP_LOGI(TAG, "%s: Trying to enter MID mode!", calibration_state_to_string(ctx));
            calibration_start(ctx);
            return;
        }

        calibration_send_state(ctx);
        calibration_update_charger_state(ctx);

        ctx->Ticks[STATE_TICK] = curTick;
    }

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[TICK]) < CALIBRATION_TIMEOUT) {
        return;
    }

    // No tick within states if done or failed, but allow above to execute so we send
    // state back to the app
    if (CAL_STATE(ctx) == Done && CAL_CSTATE(ctx) == Complete) {
        calibration_set_led_green(ctx);
        ESP_LOGI(TAG, "%s: Calibration complete!", calibration_state_to_string(ctx));
        return;
    }

    if (CAL_CSTATE(ctx) == Failed) {
        calibration_set_led_red(ctx);
        ESP_LOGE(TAG, "%s: Calibration failed!", calibration_state_to_string(ctx));
        return;
    }

    bool midMode = calibration_refresh(ctx);

    if (!midMode && !(ctx->Flags & CAL_FLAG_IDLE)) {
        ESP_LOGE(TAG, "%s: Charger exited MID mode!", calibration_state_to_string(ctx));
        CAL_CSTATE(ctx) = Failed;
        return;
    }

    bool updated = false;

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
    // TODO: Some of these may be able to be skipped for the Go? Lots of verification and
    //       warming up.
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

    if (updated) {
        ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx), charger_state_to_string(ctx));
    }

    ctx->Ticks[TICK] = xTaskGetTickCount();
}

void calibration_handle_ack(CalibrationCtx *ctx, CalibrationUdpMessage_ChargerAck *msg) {
    /* ESP_LOGI(TAG, "ChargerAck { %d }", msg->Sequence); */
}

void calibration_handle_data(CalibrationCtx *ctx, CalibrationUdpMessage_DataMessage *msg) {
    if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterVoltage_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterVoltage;
        /* ESP_LOGI(TAG, "RefMeterVoltage { %f, %f, %f }", phases.L1, phases.L2, phases.L3); */
        ctx->Ref.V[0] = phases.L1;
        ctx->Ref.V[1] = phases.L2;
        ctx->Ref.V[2] = phases.L3;
        ctx->Ticks[VOLTAGE_TICK] = xTaskGetTickCount();
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterCurrent_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterCurrent;
        /* ESP_LOGI(TAG, "RefMeterCurrent { %f, %f, %f }", phases.L1, phases.L2, phases.L3); */
        ctx->Ref.I[0] = phases.L1;
        ctx->Ref.I[1] = phases.L2;
        ctx->Ref.I[2] = phases.L3;
        ctx->Ticks[CURRENT_TICK] = xTaskGetTickCount();
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterEnergy_tag) {
        CalibrationUdpMessage_DataMessage_EnergySnapshot energy = msg->message_type.ReferenceMeterEnergy;
        ctx->Ref.E = energy.WattHours;
        ctx->Ticks[ENERGY_TICK] = xTaskGetTickCount();
    } else {
        ESP_LOGE(TAG, "Unknown CalibrationUdpMessage.Data type!");
    }
}

void calibration_reset(CalibrationCtx *ctx) {
    CalibrationServer server = ctx->Server;
    memset(ctx, 0, sizeof (*ctx));
    ctx->Server = server;
}

void calibration_handle_state(CalibrationCtx *ctx, CalibrationUdpMessage_StateMessage *msg) {
    //ESP_LOGD(TAG, "State { State = %d, Sequence = %d }", msg->State, msg->Sequence);

    bool isRun = msg->State == Starting && msg->has_Run;

    // If failed, allow starting a new run
    if (!isRun && (CAL_CSTATE(ctx) == Failed || ctx->Failure != 0)) {
        return;
    }

    if (isRun) {
        int doCalibRun = 0;

        for (pb_size_t i = 0; i < msg->Run.SelectedSerials_count; i++) {
            char *serial = msg->Run.SelectedSerials[i];
            if (strcmp(serial, devInfo.serialNumber) == 0) {
                doCalibRun = 1;
            }
        }

        if (!doCalibRun) {
            ESP_LOGE(TAG, "Invalid serial!");
        }

        if (strcmp(msg->Run.Key, CALIBRATION_KEY) != 0) {
            ESP_LOGE(TAG, "Invalid key!");
            doCalibRun = 0;
        }

        if (!doCalibRun) {
            ESP_LOGE(TAG, "Got run command from server, but was not validated!");
            return;
        } else if (ctx->Run != msg->Run.Run) {
            calibration_reset(ctx);
            ctx->Run = msg->Run.Run;
            ESP_LOGI(TAG, "Starting run %d", ctx->Run);
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
    err = inet_aton(CALIBRATION_SERVER_IP, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", CALIBRATION_SERVER_IP);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
        ESP_LOGW(TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", CALIBRATION_SERVER_IP);
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
    saddr.sin_port = htons(CALIBRATION_SERVER_PORT);
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

void calibration_task(void *pvParameters) {
    while (1) {
        while (!network_WifiIsConnected()) {
          ESP_LOGI(TAG, "Waiting for WiFi connection ...");
          vTaskDelay(3000 / portTICK_PERIOD_MS);
        }

        ESP_LOGI(TAG, "Creating UDP socket ...");

        int sock = create_multicast_ipv4_socket();
        if (sock < 0) {
            ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        serv.Socket = sock;
        bzero(&serv.ServAddr, sizeof (serv.ServAddr));
        serv.ServAddr.sin_port = htons(2020);
        serv.ServAddr.sin_family = PF_INET;
        serv.Initialized = false;

        ctx.Server = serv;
        ctx.Ticks[STATE_TICK] = xTaskGetTickCount();

        CAL_FAIL(&ctx) = 0;

        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(CALIBRATION_SERVER_PORT),
        };

        inet_aton(CALIBRATION_SERVER_IP, &sdestv4.sin_addr.s_addr);

        int i = 0;

        int err = 1;
        while (err > 0) {
            i++;

            struct timeval tv = {
                .tv_sec = 0,
                .tv_usec = 500 * 1000, // 0.5 second tick
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

                    //ESP_LOGD(TAG, "UdpMessage { Ack: %d, Data: %d, State: %d }", msg.has_Ack, msg.has_Data, msg.has_State);

                    if (msg.has_Ack) {
                        calibration_handle_ack(&ctx, &msg.Ack);
                    } 

                    if (msg.has_Data) {
                        calibration_handle_data(&ctx, &msg.Data);
                        // Emulate tick since this message comes too often to get a timeout
                        calibration_handle_tick(&ctx);
                    } 

                    if (msg.has_State) {
                        calibration_handle_state(&ctx, &msg.State);
                        ctx.LastSeq = msg.State.Sequence;
                    }

                    // Enabled use of malloc in nanopb, so free any dynamically allocated fields...
                    pb_release(CalibrationUdpMessage_fields, &msg);
                }
            } else {
                if (!ctx.Server.Initialized) {
                    /* ESP_LOGI(TAG, "Waiting for server broadcast ..."); */
                    continue;
                }

                // Timeout, do a tick
                calibration_handle_tick(&ctx);
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

    //handle = xTaskCreateStatic(calibration_task, "calibration_task", CALIBRATION_STACK_SIZE,
    //       NULL, 5, stack, &task_buffer);

    xTaskCreate(calibration_task, "calibration_task", 1024 * 5, NULL, 8, &handle);
}

void calibration_task_stop(void) {
    if (handle) {
        ESP_LOGE(TAG, "Killing calibration task!");

        shutdown(serv.Socket, 0);
        close(serv.Socket);
        serv.Initialized = false;

        calibration_reset(&ctx);

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
