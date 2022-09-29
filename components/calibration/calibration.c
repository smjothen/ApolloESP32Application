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

#include "calibration_emeter.h"

#include <calibration-message.pb.h>
#include <calibration.h>
#include <calibration_util.h>

#include <pb_decode.h>
#include <pb_encode.h>

static const char *TAG = "CALIBRATION    ";

bool calibration_tick_calibrate(CalibrationCtx *ctx) {
    ChargerState state = ctx->CState;

    switch (ctx->CState) {
        case InProgress:
            if (ctx->State == CalibrateCurrentOffset) {
                calibration_step_calibrate_current_offset(ctx);
            } else if (ctx->State == CalibrateCurrentGain) {
                calibration_step_calibrate_current_gain(ctx);
            } else if (ctx->State == CalibrateVoltageOffset) {
                calibration_step_calibrate_voltage_offset(ctx);
            } else {
                calibration_step_calibrate_voltage_gain(ctx);
            }
            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

void calibration_tick_starting_init(CalibrationCtx *ctx) {
    uint32_t calId = 0;

    if (!calibration_set_standalone(ctx, 1)) { ESP_LOGE(TAG, "Setting standalone failed!"); return; }
    if (!calibration_set_simplified_max_current(ctx, 1)) { ESP_LOGE(TAG, "Setting simplified max current failed!"); return; }
    if (!calibration_set_lock_cable(ctx, 0)) { ESP_LOGE(TAG, "Setting disable cable lock failed!"); return; }
    if (!calibration_get_calibration_id(ctx, &calId) || calId != 0) { ESP_LOGE(TAG, "Getting calibration ID failed or non-zero (%d)!", calId); return; }

    ctx->Params.CalibrationId = calId;
    ctx->InitState = true;
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
                    calibration_open_relays(ctx);
                    ESP_LOGI(TAG, "%s: Waiting for relays to open (Mode: %d) ...", calibration_state_to_string(ctx->State), ctx->Mode);
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
    calibration_close_relays(ctx);

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
                    calibration_close_relays(ctx);
                    ESP_LOGI(TAG, "%s: Waiting for relays to close (Mode: %d) ...", calibration_state_to_string(ctx->State), ctx->Mode);
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

            if (pdTICKS_TO_MS(xTaskGetTickCount() - ctx->Ticks[CURRENT_TICK]) > 2000) {
                // Ignore stale reference broadcasts... wait for more recent
                break;
            }

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

                if (ctx->Ticks[WARMUP_TICK] == 0) {
                    ctx->Ticks[WARMUP_TICK] = xTaskGetTickCount();
                } else {
                    if (xTaskGetTickCount() - ctx->Ticks[WARMUP_TICK] > minimumDuration) {
                        COMPLETE();
                        break;
                    } else {
                        ESP_LOGI(TAG, "%s: Warming up (%.1fA for %ds) ...", calibration_state_to_string(ctx->State), expectedCurrent, pdTICKS_TO_MS(minimumDuration) / 1000);
                    }
                }

            } else {
                ctx->Ticks[WARMUP_TICK] = 0;

                ESP_LOGI(TAG, "%s: Waiting to be in range (%.1fA +/- %.1f%% range, I %.1fA %.1fA %.1fA) ...",
                        calibration_state_to_string(ctx->State), expectedCurrent, allowedCurrent * 100.0, current[0], current[1], current[2]);
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
        case InProgress: {

            float totalPower;
            if (calibration_total_charge_power(ctx, &totalPower)) {
                if (totalPower <= 50.0f) {
                    if (!ctx->Ticks[STABILIZATION_TICK]) {
                        ctx->Ticks[STABILIZATION_TICK] = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
                    }

                    if (xTaskGetTickCount() > ctx->Ticks[STABILIZATION_TICK]) {
                        COMPLETE();
                    } else {
                        ESP_LOGI(TAG, "%s: Waiting for temperatures to even out ...", calibration_state_to_string(ctx->State));
                    }
                } else {
                    ctx->Ticks[STABILIZATION_TICK] = 0;
                    ESP_LOGE(TAG, "%s: Total charge power too high %.1fW > 50W!", calibration_state_to_string(ctx->State), totalPower);
                }
            } else {
                ESP_LOGE(TAG, "%s: Sending total charge power failed!", calibration_state_to_string(ctx->State));
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
            if (calibration_open_relays(ctx)) {
                COMPLETE();
            }

            break;
        case Complete:
            break;
        case Failed:
            break;
    }

    return ctx->CState != state;
}

void calibration_update_charger_state(CalibrationCtx *ctx) {
    enum ChargerOperatingMode mode = MCU_GetChargeOperatingMode();
    if (mode != ctx->Mode) {
        /* ESP_LOGI(TAG, "%s: Operation mode changed (%d -> %d) ...", calibration_state_to_string(ctx->State), ctx->Mode, mode); */
        ctx->Mode = mode;
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

void calibration_handle_tick(CalibrationCtx *ctx) {
    TickType_t curTick = xTaskGetTickCount();

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[STATE_TICK]) > CALIBRATION_TIMEOUT) {
        calibration_send_state(ctx);
        calibration_update_charger_state(ctx);

        ctx->Ticks[STATE_TICK] = curTick;
    }

    if (pdTICKS_TO_MS(curTick - ctx->Ticks[TICK]) < CALIBRATION_TIMEOUT) {
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
        case CalibrateVoltageOffset:
        case CalibrateVoltageGain:
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
        ESP_LOGI(TAG, "%s: %s ...", calibration_state_to_string(ctx->State), charger_state_to_string(ctx->CState));
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
        /* ESP_LOGI(TAG, "RefMeterEnergy { %f }", energy.WattHours); */
        ctx->Ref.E = energy.WattHours;
        ctx->Ticks[ENERGY_TICK] = xTaskGetTickCount();
    } else {
        ESP_LOGE(TAG, "Unknown CalibrationUdpMessage.Data type!");
    }
}

void calibration_handle_state(CalibrationCtx *ctx, CalibrationUdpMessage_StateMessage *msg) {
    //ESP_LOGD(TAG, "State { State = %d, Sequence = %d }", msg->State, msg->Sequence);

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
            //ESP_LOGI(TAG, "Starting run %d", ctx->Run);
        }
    }

    if (msg->Sequence < ctx->LastSeq) {
        ESP_LOGE(TAG, "Received packet out of sequence! %d <= %d, Ignoring...", msg->Sequence, ctx->LastSeq);
        return;
    }

    if (ctx->State != msg->State) {
        /* ESP_LOGI(TAG, "%s", calibration_state_to_string(msg->State)); */
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


    /*
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == -1) {
        ESP_LOGE(TAG, "Failed to set SO_BROADCAST");
        goto err;
    }
    */

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

        CalibrationCtx ctx = { 0 };

        CalibrationServer serv;
        serv.Socket = sock;
        bzero(&serv.ServAddr, sizeof (serv.ServAddr));
        serv.ServAddr.sin_port = htons(2020);
        serv.ServAddr.sin_family = PF_INET;

        ctx.Server = serv;
        ctx.HaveServer = false;
        ctx.Ticks[STATE_TICK] = xTaskGetTickCount();
        ctx.Mode = CHARGE_OPERATION_STATE_UNINITIALIZED;

        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(CALIBRATION_SERVER_PORT),
        };

        inet_aton(CALIBRATION_SERVER_IP, &sdestv4.sin_addr.s_addr);

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
                        ctx.LastSeq = msg.State.Sequence;
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
