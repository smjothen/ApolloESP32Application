#!/usr/bin/env python3
import types
import asyncio
import logging
from datetime import datetime, timedelta
import time

from websockets.frames import CloseCode
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

async def test_reconnect(cp):
    logging.info("testing reconnect")
    global expecting_boot_notification

    may_send_status_codes = [close_status for close_status in CloseCode if close_status != 1005 and close_status != 1006 and close_status != 1015]
    for close_method in may_send_status_codes:
        expecting_boot_notification = True

        logging.info(f'Closing with code: {close_method}')
        await cp._connection.close(code=close_method, reason='Testing ' + str(close_method))

        i = 0
        while expecting_boot_notification:
            i += 1
            if i > 30:
                logging.error(f'Websocket did not reconnect after {close_method}')
                return False

            logging.warning("Waiting for boot notification")
            await asyncio.sleep(3)

    return True

async def test_websocket(cp, include_manual_tests = True):
    logging.info('Setting up websocket test')
    preconfig_res = await ensure_configuration(cp,{ConfigurationKey.local_pre_authorize: "false",
                                                   ConfigurationKey.authorize_remote_tx_requests: "true",
                                                   ConfigurationKey.heartbeat_interval: "0",
                                                   "AuthorizationRequired": "true",
                                                   ConfigurationKey.connection_time_out: "10",
                                                   ConfigurationKey.clock_aligned_data_interval: "0",
                                                   ConfigurationKey.meter_value_sample_interval: "0",
                                                   ConfigurationKey.minimum_status_duration: "0"});

    if preconfig_res != 0:
        return -1

    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("Weboscket test got boot msg")
        global expecting_boot_notification
        expecting_boot_notification = False

        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=15,
            status=cp.registration_status
        )

    cp.route_map[Action.BootNotification]["_on_action"] = types.MethodType(on_boot_notitication, cp)

    if await test_reconnect(cp) != True:
       return False

    return True
