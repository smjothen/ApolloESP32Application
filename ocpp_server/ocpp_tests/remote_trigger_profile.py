#!/usr/bin/env python3

import types
import asyncio
import logging
from datetime import datetime, timedelta
import time

from ocpp.routing import on
from ocpp.v16 import call_result, call
from ocpp.v16.enums import (
    ChargePointErrorCode,
    ConfigurationKey,
    ChargePointStatus,
    Action,
    Measurand,
    DiagnosticsStatus,
    FirmwareStatus,
    MessageTrigger,
    TriggerMessageStatus
)

from datetime import(
    datetime,
    timezone,
    timedelta
)

from ocpp.v16.datatypes import(
    IdTagInfo,
)

from ocpp_tests.test_utils import ensure_configuration

got_boot_notification = False

got_diagnostics_status_notification = False
upload_status = DiagnosticsStatus.upload_failed

got_firmware_status_notification = False
firmware_status = FirmwareStatus.installation_failed

got_heartbeat = False
got_meter_values = False
got_status_notification = False

async def test_remote_trigger_disallow_new_boot(cp):
    while(cp.connector1_status == ChargePointStatus.unavailable):
        logging.warning(f"Waiting to exit status unavailable...({cp.connector1_status})")
        await asyncio.sleep(2)

    global got_boot_notification
    got_boot_notification = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.boot_notification))
    if result is None or result.status != TriggerMessageStatus.rejected:
        logging.error(f"Trigger was not rejected as recommended by errata v4.0 for {MessagTrigger.boot_notification}: {result}")
        return False

    i = 0
    while not got_boot_notification and i < 3:
        i+=1
        logging.warning(f"Waiting for unexpected boot notification...")
        await asyncio.sleep(2)

    if got_boot_notification:
        logging.error("Got unexpected boot notification")
        return False

    return True

async def test_remote_trigger_firmware_status_idle(cp):

    global got_firmware_status_notification
    got_firmware_status_notification  = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.firmware_status_notification))
    if result is None or result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger not accepted for {MessagTrigger.firmware_status_notification}: {result}")
        return False

    i = 0
    while not got_firmware_status_notification:
        if i > 3:
            logging.error("Did not get a firmware status notification")
            return False

        logging.warning("Waiting for firmware status...")
        await asyncio.sleep(2)

    if firmware_status != FirmwareStatus.idle:
        logging.error("Expected firmware status to be idle")
        return False

    global got_diagnostics_status_notification
    got_diagnostics_status_notification  = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.diagnostics_status_notification))
    if result is None or result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger not accepted for {MessagTrigger.diagnostics_status_notification}: {result}")
        return False

    i = 0
    while not got_diagnostics_status_notification:
        if i > 3:
            logging.error("Did not get a diagnostics status notification")
            return False

        logging.warning("Waiting for diagnostics status...")
        await asyncio.sleep(2)

    if upload_status != DiagnosticsStatus.idle:
        logging.error("Expected diagnostics upload status to be idle")
        return False

    return True

async def test_remote_trigger_always_accepted(cp):
    global got_heartbeat
    got_heartbeat = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.heartbeat))
    if result is None or result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger not accepted for {MessagTrigger.heartbeat}: {result}")
        return False

    i = 0
    while not got_heartbeat:
        if i > 3:
            logging.error("Did not get a heartbeat")
            return False

        logging.warning("Waiting for heartbeat...")
        await asyncio.sleep(2)

    global got_meter_values
    got_meter_values = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.meter_values))
    if result is None or result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger not accepted for {MessagTrigger.meter_values}: {result}")
        return False

    i = 0
    while not got_meter_values:
        if i > 3:
            logging.error("Did not get a meter_values")
            return False

        logging.warning("Waiting for meter_values...")
        await asyncio.sleep(2)

    global got_status_notification
    got_status_notification = False
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.status_notification))
    if result is None or result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger not accepted for {MessagTrigger.status_notification}: {result}")
        return False

    i = 0
    while not got_status_notification:
        if i > 3:
            logging.error("Did not get a status_notification")
            return False

        logging.warning("Waiting for status_notification...")
        await asyncio.sleep(2)

    return True

async def test_remote_trigger_profile(cp, include_manual_tests = True):
    logging.info('Setting up remote trigger profile test')
    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.local_pre_authorize: "false",
                                                    ConfigurationKey.authorize_remote_tx_requests: "false",
                                                    ConfigurationKey.heartbeat_interval: "0",
                                                    ConfigurationKey.clock_aligned_data_interval: "0",
                                                    ConfigurationKey.meter_value_sample_interval: "0",
                                                    ConfigurationKey.meter_values_aligned_data: Measurand.current_import,
                                                    ConfigurationKey.meter_values_sampled_data: Measurand.current_offered,
                                                    ConfigurationKey.connection_time_out: "5",
                                                    "AuthorizationRequired": "true"})

    if preconfig_res != 0:
        return False

    def on_boot_notification(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info(f"Trigger test got {Action.BootNotification}")

        global got_boot_notification
        got_boot_notification = True

        self.registration_status = RegistrationStatus.accepted

        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=0,
            status=cp.registration_status
        )

    def on_diagnostics_status_notification(self, status):
        logging.info(f"Trigger test got {Action.DiagnosticsStatusNotification}: {status}")

        global got_diagnostics_status_notification
        global upload_status
        got_diagnostics_status_notification = True
        upload_status = status

        return call_result.DiagnosticsStatusNotificationPayload()

    def on_firmware_status_notification(self, status):
        logging.info(f"Trigger test got {Action.FirmwareStatusNotification}: {status}")

        global got_firmware_status_notification
        global firmware_status
        got_firmware_status_notification = True
        firmware_status = status

        return call_result.FirmwareStatusNotificationPayload()

    def on_heartbeat(self, **kwargs):
        logging.info(f"Trigger test got {Action.Heartbeat}")

        global got_heartbeat
        got_heartbeat = True

        time = datetime.utcnow().replace(tzinfo=timezone.utc).isoformat()
        time = datetime.utcnow().isoformat() + 'Z'

        return call_result.HeartbeatPayload(
            current_time=time,
        )

    def on_meter_values(self, connector_id, meter_value, **kwargs):
        logging.info(f"Trigger test got {Action.MeterValues}")

        global got_meter_values
        got_meter_values = True

        return call_result.MeterValuesPayload()

    def on_status_notification(self, connector_id, error_code, status, **kwargs):
        logging.info(f"Trigger test got {Action.StatusNotification}")

        global got_status_notification
        got_status_notification = True

        if(error_code == ChargePointErrorCode.no_error):
            logging.info(f'CP status: {status} ({error_code})')
            self.connector1_status = status
        else:
            logging.error(f'CP status: {status} ({error_code})')

        return call_result.StatusNotificationPayload()

    cp.route_map[Action.BootNotification]["_on_action"] = types.MethodType(on_boot_notification, cp)
    cp.route_map[Action.DiagnosticsStatusNotification]["_on_action"] = types.MethodType(on_diagnostics_status_notification, cp)
    cp.route_map[Action.FirmwareStatusNotification]["_on_action"] = types.MethodType(on_firmware_status_notification, cp)
    cp.route_map[Action.Heartbeat]["_on_action"] = types.MethodType(on_heartbeat, cp)
    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_values, cp)
    cp.route_map[Action.StatusNotification]["_on_action"] = types.MethodType(on_status_notification, cp)

    if await test_remote_trigger_disallow_new_boot(cp) != True:
        return False

    if await test_remote_trigger_firmware_status_idle(cp) != True:
        return False

    if await test_remote_trigger_always_accepted(cp) != True:
        return False

    return True
