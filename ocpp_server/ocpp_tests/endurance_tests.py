#!/usr/bin/env python3
import types
import asyncio
import logging
from datetime import datetime, timedelta
import time

from ocpp.routing import on
from ocpp.v16 import call_result, call
from ocpp.v16.enums import (
    AvailabilityStatus,
    AvailabilityType,
    ConfigurationKey,
    ConfigurationStatus,
    ChargePointStatus,
    AuthorizationStatus,
    Action,
    RemoteStartStopStatus,
    ResetStatus,
    ResetType,
    TriggerMessageStatus,
    MessageTrigger,
    RegistrationStatus,
    UnlockStatus,
    Measurand,
    Phase
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import ensure_configuration

expecting_boot_notification = False
unexpected_boot_count = 0
expected_boot_count = 0

async def boot_repeat(cp):
    global expecting_boot_notification
    global unexpected_boot_count
    global expected_boot_count

    unexpected_boot_count = 0
    expected_boot_count = 0

    logging.info("Starting boot repeat test")

    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("Test got boot msg")
        global expecting_boot_notification
        if expecting_boot_notification:
            expecting_boot_notification = False
        else:
            global unexpected_boot_count
            unexpected_boot_count += 1
            logging.error(f'Got unexpected boot: {unexpected_boot_count}')
        return call_result.BootNotificationPayload(current_time=datetime.utcnow().isoformat(), interval=0, status=RegistrationStatus.accepted)

    cp.route_map[Action.BootNotification]["_on_action"] = types.MethodType(on_boot_notitication, cp)

    preconfig_res = await ensure_configuration(cp,{ConfigurationKey.local_pre_authorize: "false",
                                                   ConfigurationKey.authorize_remote_tx_requests: "true",
                                                   ConfigurationKey.heartbeat_interval: "0",
                                                   "AuthorizationRequired": "true",
                                                   ConfigurationKey.connection_time_out: "120",
                                                   ConfigurationKey.clock_aligned_data_interval: "0",
                                                   ConfigurationKey.meter_value_sample_interval: "0",
                                                   ConfigurationKey.minimum_status_duration: "0",
                                                   "MessageTimeout": "30"});

    if preconfig_res != 0:
        logging.error("Unable to configure for ")
        return

    while True:
        i = 0
        while(cp.connector1_status == ChargePointStatus.unavailable):
            logging.warning(f"Waiting for status not unavailable...({cp.connector1_status})")
            i += 1
            if i > 10:
                logging.error(f"Charger never exited unavailable during endurance test for boot. Got {expected_boot_count} expected boots and {unexpected_boot_count} unexpected boots")
                return False
            await asyncio.sleep(5)

        await asyncio.sleep(10)

        expecting_boot_notification = True
        result = await cp.call(call.ResetPayload(type=ResetType.soft if expected_boot_count % 2 == 0 else ResetType.hard))
        if result.status != ResetStatus.accepted:
            logging.error(f"Failed to call Reset during endurance test for boot. Got {expected_boot_count} expected boots and {unexpected_boot_count} unexpected boots")
            return False

        i = 0
        while expecting_boot_notification:
            i += 1
            if i > 40:
                logging.error(f"Did not get expected boot during endurance test for boot. Got {expected_boot_count} expected boots and {unexpected_boot_count} unexpected boots")
                return False

            logging.warning("Awaiting new boot notification...")
            await asyncio.sleep(3)

        expected_boot_count += 1
        logging.info(f"Endurance boot state: {expected_boot_count} expected boots and {unexpected_boot_count} unexpected boots")


async def endurance_tests(cp):
    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, 'Input sub id [B]oot repeat')

    if response == "B":
        await boot_repeat(cp)


    logging.error("Endurance test exited")
