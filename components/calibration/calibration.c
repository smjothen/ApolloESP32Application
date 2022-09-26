/* UDP MultiCast Send/Receive Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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
#include "emeter.h"

#include <calibration-message.pb.h>
#include <calibration.h>
#include <pb_decode.h>
#include <pb_encode.h>

#define UDP_PORT 3333
#define MULTICAST_TTL 1
#define MULTICAST_IPV4_ADDR "232.10.11.12"
#define LISTEN_ALL_IF

static const char *TAG = "CALIBRATION    ";

// Milliseconds..
#define STATE_TIMEOUT 1000 
#define TICK_TIMEOUT 1000

#define STATE(s) (ctx->CState = (s))
#define COMPLETE() STATE(Complete)
#define FAILED() STATE(Failed)

#define STEP(s) (ctx->CStep = (s))

void calibration_check_timeout(CalibrationCtx *ctx);
int calibration_send_state(CalibrationCtx *ctx);
void calibration_update_charger_state(CalibrationCtx *ctx);

static const char *calibration_state_to_string(CalibrationState state) {
    const char *_calibration_states[] = { FOREACH_CS(CS_STRING) };
    size_t max_state = sizeof (_calibration_states) / sizeof (_calibration_states[0]);
    if (state < 0 || state > max_state || !_calibration_states[state]) {
        return "UnknownCalibrationState";
    }
    return _calibration_states[state];
}

static const char *calibration_step_to_string(CalibrationStep state) {
    const char *_calibration_steps[] = { FOREACH_CLS(CS_STRING) };
    size_t max_state = sizeof (_calibration_steps) / sizeof (_calibration_steps[0]);
    if (state < 0 || state > max_state || !_calibration_steps[state]) {
        return "UnknownStep";
    }
    return _calibration_steps[state];
}

static const char *charger_state_to_string(ChargerState state) {
    const char *_charger_states[] = { FOREACH_CHS(CS_STRING) };
    size_t max_state = sizeof (_charger_states) / sizeof (_charger_states[0]);
    if (state < 0 || state > max_state || !_charger_states[state]) {
        return "UnknownChargerState";
    }
    return _charger_states[state];
}

bool calibration_get_emeter_snapshot(CalibrationCtx *ctx, uint8_t *source, float *ivals, float *vvals) {
    MessageType ret;

    if ((ret = MCU_SendCommandId(CommandCurrentSnapshot)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send current snapshot command!");
        return false;
    }

    if (!MCU_GetEmeterSnapshot(ParamEmeterVoltageSnapshot, source, ivals)) {
        ESP_LOGE(TAG, "Couldn't get current snapshot!");
        return false;
    }

    if ((ret = MCU_SendCommandId(CommandVoltageSnapshot)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send voltage snapshot command!");
        return false;
    }

    if (!MCU_GetEmeterSnapshot(ParamEmeterVoltageSnapshot, source, vvals)) {
        ESP_LOGE(TAG, "Couldn't get voltage snapshot!");
        return false;
    }

    return true;
}

CalibrationType calibration_state_to_type(CalibrationState state) {
    switch (state) {
        case CalibrateCurrentOffset: return CALIBRATION_TYPE_CURRENT_OFFSET;
        case CalibrateCurrentGain: return CALIBRATION_TYPE_CURRENT_GAIN;
        case CalibrateVoltageOffset: return CALIBRATION_TYPE_VOLTAGE_OFFSET;
        case CalibrateVoltageGain: return CALIBRATION_TYPE_VOLTAGE_GAIN;
        default: return CALIBRATION_TYPE_NONE;
    }
}

int calibration_state_to_samples(CalibrationState state) {
    switch (state) {
        case CalibrateCurrentOffset:
        case CalibrateVoltageOffset: return 20 * 5;
        case CalibrateCurrentGain:
        case CalibrateVoltageGain: return 17;
        default: return 0;
    }
}

uint16_t calibration_read_samples(void) {
    ZapMessage msg = MCU_ReadParameter(ParamCalibrationSamples);
    if (msg.type == MsgReadAck && msg.identifier == ParamCalibrationSamples && msg.length == 2) {
        return GetUInt16(msg.data);
    }
    return 0;
}

bool calibration_read_average(int phase, float *average) {
    ZapMessage msg = MCU_ReadParameter(ParamCalibrationAveragePhase1 + phase);
    if (msg.type == MsgReadAck && msg.identifier == (ParamCalibrationAveragePhase1 + phase) && msg.length == 4) {
        *average = GetFloat(msg.data);
        return true;
    }
    return false;
}

uint16_t calibration_get_emeter_averages(CalibrationCtx *ctx, float *averages) {
    uint16_t samples = calibration_read_samples();

    if (samples == calibration_state_to_samples(ctx->State)) {
        for (int phase = 0; phase < 3; phase++) {
            if (!calibration_read_average(phase, &averages[phase])) {
                ESP_LOGE(TAG, "Couldn't read phase %d average!", phase);
                return 0;
            }
        }
        return samples;
    } else {
        ESP_LOGE(TAG, "Unexpected samples: %d", samples);
    }

    return 0;
}


bool calibration_step_calibrate(CalibrationCtx *ctx) {
    CalibrationStep step = ctx->CStep;

    switch (ctx->CStep) {
        case InitRelays:
            // For now we just skip this but should probably ensure relays are
            // open/closed...

            // Treat zero stabilization tick as init/pre-calibration step
            if (!ctx->StabilizationTick) {
                for (int phase = 0; phase < 3; phase++) {
                    emeter_write_float(I1_OFFS + phase, 0.0, 23);
                    emeter_write_float(I1_GAIN + phase, 1.0, 21);
                    emeter_write_float(V1_OFFS + phase, 0.0, 23);
                    emeter_write_float(V1_GAIN + phase, 1.0, 21);
                }

                if (ctx->State == CalibrateCurrentOffset) {
                    emeter_write_float(HPF_COEF_I, 0.0, 23);
                    emeter_write_float(IARMS_OFF, 0.0, 23);
                    emeter_write_float(IBRMS_OFF, 0.0, 23);
                    emeter_write_float(ICRMS_OFF, 0.0, 23);
                } else if (ctx->State == CalibrateCurrentGain) {
                    emeter_write_float(IARMS_OFF, 0.0, 23);
                    emeter_write_float(IBRMS_OFF, 0.0, 23);
                    emeter_write_float(ICRMS_OFF, 0.0, 23);
                } else if (ctx->State == CalibrateVoltageOffset) {
                    // TODO ? 
                } else if (ctx->State == CalibrateVoltageGain) {
                    // TODO ?
                }
            }

            // Setup stabilization for next step 
            switch(ctx->State) {
                case CalibrateCurrentGain  : ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(20000); break;
                case CalibrateVoltageOffset: ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(10000); break;
                case CalibrateVoltageGain  : ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(0); break;
                case CalibrateCurrentOffset: ctx->StabilizationTick = xTaskGetTickCount() + pdMS_TO_TICKS(0); break;
                default: break;
            }

            STEP(Stabilization);
            break;
        case Stabilization:

            if (xTaskGetTickCount() > ctx->StabilizationTick) {
                STEP(InitCalibration);
            } else {
                ESP_LOGI(TAG, "Stabilizing %d ...", ctx->StabilizationTick - xTaskGetTickCount());
            }

            break;
        case InitCalibration: {
            CalibrationType type = calibration_state_to_type(ctx->State);
            MCU_SendUint8Parameter(ParamRunCalibration, type);
            STEP(Calibrating);
            break;
        }
        case Calibrating: {
            float avg[3];

            if (calibration_get_emeter_averages(ctx, avg)) {
                ESP_LOGI(TAG, "AVG: %f %f %f", avg[0],avg[1],avg[2]);

                // Start for verification
                CalibrationType type = calibration_state_to_type(ctx->State);
                MCU_SendUint8Parameter(ParamRunCalibration, type);
     
                STEP(Verify);
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

bool calibration_tick_calibrate(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;
    CalibrationStep step = ctx->CStep;

    switch (ctx->CState) {
        case InProgress:
            calibration_step_calibrate(ctx);
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    if (ctx->CStep != step) {
        ESP_LOGI(TAG, "CalibrationStep %s -> %s", calibration_step_to_string(step), calibration_step_to_string(ctx->CStep));
    }

    return ctx->CState != state;
}

void calibration_tick_starting_init(CalibrationCtx *ctx) {
    MessageType ret;

    if ((ret = MCU_SendCommandId(CommandStopChargingFinal)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send start charing command!");
        return;
    }

    if ((ret = MCU_SendUint8Parameter(ParamIsStandalone, 1)) != MsgWriteAck) {
        ESP_LOGE(TAG, "Couldn't set standalone mode!");
        return;
    }

    if ((ret = MCU_SendFloatParameter(ParamSimplifiedModeMaxCurrent, 32.0)) != MsgWriteAck) {
        ESP_LOGE(TAG, "Couldn't set simplified mode max current!");
        return;
    }

    if ((ret = MCU_SendUint8Parameter(LockCableWhenConnected, 0)) != MsgWriteAck) {
        ESP_LOGE(TAG, "Couldn't disable cable lock!");
        return;
    }

    if (!MCU_GetMidStoredCalibrationId(&ctx->CalibrationId)) {
        ESP_LOGE(TAG, "Couldn't read calibration ID!");
        return;
    }

    if (ctx->CalibrationId != 0) {
        ESP_LOGE(TAG, "Reading calibration ID, expected 0 but got %d!", ctx->CalibrationId);
        return;
    }

    ctx->InitState = true;
    ctx->InitTick = xTaskGetTickCount();
}

bool calibration_tick_starting(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress: {

            // Special pseudo-init state
            if (!ctx->InitState) {
                calibration_tick_starting_init(ctx);
            } else {

                // Stop command seems to cause it to go to paused state so accept that too?
                if ((ctx->Mode == CHARGE_OPERATION_STATE_STOPPED || ctx->Mode == CHARGE_OPERATION_STATE_PAUSED)) {
                    COMPLETE();
                } else {
                    ESP_LOGI(TAG, "Waiting for CHARGE_OPERATION_STATE_STOPPED currently %d ...", ctx->Mode);
                }

            }

            break;
        }
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_contact_cleaning(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

void calibration_tick_close_relays_init(CalibrationCtx *ctx) {
    MessageType ret;

    if ((ret = MCU_SendCommandId(CommandResumeChargingMCU)) != MsgCommandAck) {
        ESP_LOGE(TAG, "Couldn't send resume charging command!");
        return;
    }

    ctx->InitTick = xTaskGetTickCount();
    ctx->InitState = true;

}

bool calibration_tick_close_relays(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress: {
            if (!ctx->InitState) {
                calibration_tick_close_relays_init(ctx);
            } else {

                if (ctx->Mode == CHARGE_OPERATION_STATE_CHARGING) {
                    COMPLETE();
                } else {
                    ESP_LOGI(TAG, "Waiting for CHARGE_OPERATION_STATE_CHARGING currently %d ...", ctx->Mode);
                }

            }
            break;
        }
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
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
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress: {
            uint8_t source;
            float voltage[3];
            float current[3];

            if (!calibration_get_emeter_snapshot(ctx, &source, current, voltage)) {
                break;
            }

            if (pdTICKS_TO_MS(xTaskGetTickCount() - ctx->LastITick) > 2000) {
                // Ignore stale reference broadcasts... wait for more recent
                break;
            }

            //ESP_LOGI(TAG, "V: %f %f %f I: %f %f %f", voltage[0], voltage[1], voltage[2], current[0], current[1], current[2]);

            float expectedCurrent = 5.0;
            float expectedVoltage = 230.0;

            float allowedCurrent = 0.1;
            float allowedVoltage = 0.1;

            TickType_t minimumDuration;

            if (ctx->WarmupOptions & MediumLevelCurrent) {
                expectedCurrent = 10.0;
                minimumDuration = pdMS_TO_TICKS(5 * 1000);
            } else if (ctx->WarmupOptions & HighLevelCurrent) {
                expectedCurrent = 32.0;
                minimumDuration = pdMS_TO_TICKS(5 * 1000);
            } else {
                expectedCurrent = 0.5;
                allowedCurrent = 0.5;
                minimumDuration = pdMS_TO_TICKS(30 * 1000);
            }

            if (calibration_phases_within(current, expectedCurrent, allowedCurrent) == 3
             && calibration_phases_within(voltage, expectedVoltage, allowedVoltage) == 3) {

                if (ctx->WarmupTick == 0) {
                    ctx->WarmupTick = xTaskGetTickCount();
                } else {
                    if (xTaskGetTickCount() - ctx->WarmupTick > minimumDuration) {
                        COMPLETE();
                        break;
                    } else {
                        ESP_LOGI(TAG, "Warming up @ %.1fA for %dms ...", expectedCurrent, pdTICKS_TO_MS(minimumDuration));
                    }
                }

            } else {
                ctx->WarmupTick = 0;

                ESP_LOGI(TAG, "Waiting for warm up ... %.1fA +/- %.1f%% range ... I %.1f %.1f %.1f ...", expectedCurrent, allowedCurrent * 100.0, current[0], current[1], current[2]);
            }

            break;
        }
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_warmup_steady_state_temp(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_write_calibration_params(CalibrationCtx *ctx) {

    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_verification_start(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_verification_running(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_verification_done(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

bool calibration_tick_done(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            COMPLETE();
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

void calibration_handle_tick(CalibrationCtx *ctx) {
    TickType_t curTick = xTaskGetTickCount();

    if (pdTICKS_TO_MS(curTick - ctx->StateTick) > STATE_TIMEOUT) {
        calibration_send_state(ctx);
        calibration_update_charger_state(ctx);

        ctx->StateTick = curTick;
    }

    if (pdTICKS_TO_MS(curTick - ctx->LastTick) < TICK_TIMEOUT) {
        return;
    }

    bool updated = false;

    switch(ctx->State) {
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
            updated = calibration_tick_calibrate(ctx);
            break;
        case CalibrateVoltageOffset:
            updated = calibration_tick_calibrate(ctx);
            break;
        case CalibrateVoltageGain:
            updated = calibration_tick_calibrate(ctx);
            break;
        case CalibrateCurrentGain:
            updated = calibration_tick_calibrate(ctx);
            break;
        case VerificationStart:
            updated = calibration_tick_verification_start(ctx);
            break;
        case VerificationRunning:
            updated = calibration_tick_verification_running(ctx);
            break;
        case VerificationDone:
            updated = calibration_tick_verification_done(ctx);
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
        ESP_LOGI(TAG, "STATE %s: %s ...", calibration_state_to_string(ctx->State), charger_state_to_string(ctx->CState));
    }

    ctx->LastTick = xTaskGetTickCount();
}

void calibration_handle_ack(CalibrationCtx *ctx, CalibrationUdpMessage_ChargerAck *msg) {
    /* ESP_LOGI(TAG, "ChargerAck { %d }", msg->Sequence); */
}

void calibration_handle_data(CalibrationCtx *ctx, CalibrationUdpMessage_DataMessage *msg) {
    if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterVoltage_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterVoltage;
        /* ESP_LOGI(TAG, "RefMeterVoltage { %f, %f, %f }", phases.L1, phases.L2, phases.L3); */
        ctx->V[0] = phases.L1;
        ctx->V[1] = phases.L2;
        ctx->V[2] = phases.L3;
        ctx->LastVTick = xTaskGetTickCount();
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterCurrent_tag) {
        CalibrationUdpMessage_DataMessage_PhaseSnapshot phases = msg->message_type.ReferenceMeterCurrent;
        /* ESP_LOGI(TAG, "RefMeterCurrent { %f, %f, %f }", phases.L1, phases.L2, phases.L3); */
        ctx->I[0] = phases.L1;
        ctx->I[1] = phases.L2;
        ctx->I[2] = phases.L3;
        ctx->LastITick = xTaskGetTickCount();
    } else if (msg->which_message_type == CalibrationUdpMessage_DataMessage_ReferenceMeterEnergy_tag) {
        CalibrationUdpMessage_DataMessage_EnergySnapshot energy = msg->message_type.ReferenceMeterEnergy;
        /* ESP_LOGI(TAG, "RefMeterEnergy { %f }", energy.WattHours); */
        ctx->E = energy.WattHours;
        ctx->LastETick = xTaskGetTickCount();
    } else {
        ESP_LOGE(TAG, "Unknown CalibrationUdpMessage.Data type!");
    }
}

int calibration_send_state(CalibrationCtx *ctx) {
    char buf[128];

    struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();

    ChargerStateUdpMessage reply = ChargerStateUdpMessage_init_zero;
    reply.Serial = devInfo.serialNumber;
    reply.State = ctx->CState;
    reply.StateAck = ctx->State;
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
    /*         calibration_state_to_string(ctx->State), */
    /*         calibration_step_to_string(ctx->CStep), */
    /*         charger_state_to_string(ctx->CState)); */

    return ctx->Seq++;
}

void calibration_update_charger_state(CalibrationCtx *ctx) {
    enum ChargerOperatingMode mode = MCU_GetChargeOperatingMode();
    if (mode != ctx->Mode) {
        ESP_LOGI(TAG, "ChargerOperationMode %d -> %d", ctx->Mode, mode);
        ctx->Mode = mode;
    }
}

void calibration_check_timeout(CalibrationCtx *ctx) {
    TickType_t curTick = xTaskGetTickCount();

    if (pdTICKS_TO_MS(curTick - ctx->StateTick) > STATE_TIMEOUT) {
        calibration_send_state(ctx);
        calibration_update_charger_state(ctx);

        ctx->StateTick = curTick;
    }
}

void calibration_handle_state(CalibrationCtx *ctx, CalibrationUdpMessage_StateMessage *msg) {
    ESP_LOGD(TAG, "State { State = %d, Sequence = %d }", msg->State, msg->Sequence);

    struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();

    if (msg->State == Starting && msg->has_Run) {
        int doCalibRun = 0;

        for (pb_size_t i = 0; i < msg->Run.SelectedSerials_count; i++) {
            char *serial = msg->Run.SelectedSerials[i];
            if (strcmp(serial, devInfo.serialNumber) == 0) {
                doCalibRun = 1;
            }
        }

        if (strcmp(msg->Run.Key, CALIBRATION_KEY) != 0) {
            doCalibRun = 0;
        }

        if (!doCalibRun) {
            ESP_LOGE(TAG, "Got run command from server, but was not validated!");
            return;
        } else if (ctx->Run != msg->Run.Run) {
            ctx->Run = msg->Run.Run;
            ESP_LOGI(TAG, "Starting run %d", ctx->Run);
        }
    }

    if (msg->Sequence < ctx->LastSeq) {
        ESP_LOGE(TAG, "Received packet out of sequence! %d <= %d, Ignoring...", msg->Sequence, ctx->LastSeq);
        return;
    }

    if (ctx->State != msg->State) {
        ESP_LOGI(TAG, "STATE: %s", calibration_state_to_string(msg->State));
    } else {
        return;
    }

    if (msg->has_Verification) {
        if (ctx->State == WarmingUp) {
            ctx->WarmupOptions = msg->Verification.TestId;
        }
    }

    ctx->State = msg->State;
    // Allow pseudo `init' step
    ctx->InitState = false;

    ctx->CState = InProgress;
    ctx->CStep = InitRelays;
}

static int socket_add_ipv4_multicast_group(int sock, bool assign_source_if) {
    struct ip_mreq imreq = { 0 };
    struct in_addr iaddr = { 0 };
    int err = 0;
    // Configure source interface
#ifdef LISTEN_ALL_IF
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
    err = inet_aton(MULTICAST_IPV4_ADDR, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", MULTICAST_IPV4_ADDR);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
        ESP_LOGW(TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", MULTICAST_IPV4_ADDR);
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
    saddr.sin_port = htons(UDP_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        goto err;
    }


    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
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

        int sock;

        sock = create_multicast_ipv4_socket();
        if (sock < 0) {
            ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        CalibrationCtx ctx = { 0 };

        CalibrationServer serv;
        serv.Socket = sock;
        bzero(&serv.ServAddr, sizeof (serv.ServAddr));
        serv.ServAddr.sin_port = htons(2020);
        serv.ServAddr.sin_family = PF_INET;

        ctx.Server = serv;
        ctx.HaveServer = false;
        ctx.StateTick = xTaskGetTickCount();
        ctx.Mode = CHARGE_OPERATION_STATE_UNINITIALIZED;

        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(UDP_PORT),
        };

        inet_aton(MULTICAST_IPV4_ADDR, &sdestv4.sin_addr.s_addr);

        int err = 1;
        while (err > 0) {
            struct timeval tv = {
                .tv_sec = 0,
                .tv_usec = 500 * 1000, // 0.5 second tick
            };

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int s = select(sock + 1, &rfds, NULL, NULL, &tv);

            if (s < 0) {
                ESP_LOGE(TAG, "Select failed: %d", errno);
                err = -1;
                break;
            } else if (s > 0) {
                if (FD_ISSET(sock, &rfds)) {
                    // Incoming datagram received
                    uint8_t recvbuf[128];
                    char raddr_name[32] = { 0 };

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

                    CalibrationUdpMessage msg = CalibrationUdpMessage_init_zero;
                    pb_istream_t strm = pb_istream_from_buffer(recvbuf, len);

                    if (!pb_decode(&strm, CalibrationUdpMessage_fields, &msg)) {
                        ESP_LOGE(TAG, "Decoding calibration message failed: %s", PB_GET_ERROR(&strm));
                        continue;
                    }

                    ctx.Server.ServAddr.sin_addr.s_addr = ((struct sockaddr_in *)&raddr)->sin_addr.s_addr;
                    ctx.HaveServer = true;

                    ESP_LOGD(TAG, "UdpMessage { Ack: %d, Data: %d, State: %d }", msg.has_Ack, msg.has_Data, msg.has_State);

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
                    }

                    // Enabled use of malloc in nanopb, so free any dynamically allocated fields...
                    pb_release(CalibrationUdpMessage_fields, &msg);
                }
            } else {
                if (!ctx.HaveServer) {
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
