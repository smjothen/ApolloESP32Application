#!/usr/bin/env python3
import types
import asyncio
import logging
from datetime import datetime, timedelta
import time

from ocpp.routing import on
from ocpp.v16 import call_result, call
from ocpp.v16.enums import (
    ConfigurationKey,
    ConfigurationStatus,
    ChargePointStatus,
    AuthorizationStatus,
    Action,
    RemoteStartStopStatus
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import ensure_configuration
expecting_new_rfid = False
new_rfid = ''

async def test_got_presented_rfid(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    global expecting_new_rfid
    expecting_new_rfid = True

    while(expecting_new_rfid):
        logging.warning(f"Waiting for RFID tag...")
        await asyncio.sleep(3)

    global new_rfid
    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, f'does "{new_rfid}" match presented id? y/n')

    if response != "y":
        logging.error(f'Authorize got incorrect RFID. User typed {response}')
        return False

    response = await loop.run_in_executor(None, input, f'Did the CP indicate accepted via LED and sound? y/n')
    if response != "y":
        logging.error(f'Unexpected LED: {response}')
        return False

    return True

async def test_remote_start(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    global expecting_new_rfid
    expecting_new_rfid = True
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in available state")
        return False

    while(expecting_new_rfid):
        logging.warning("Waiting for Authorize.req...")
        await asyncio.sleep(1)

    if new_rfid != "test_tag":
        logging.error(f"Unexpected rfid tag in Authorize.req :{new_rfid}")
        return False

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.preparing:
        logging.error(f'CP did not enter preparing after accepted Authorize.req: {cp.connector1_status}')
        return False

    i = 0
    while cp.connector1_status != ChargePointStatus.available:
        i+=1
        if i > 8:
            logging.error(f'CP did not enter avaialble after connection timeout')
            return False
        logging.warning(f'Awaiting connection timeout')
        await asyncio.sleep(2)

    return True

async def test_core_profile(cp, include_manual_tests = True):
    logging.info('Setting up core profile test')
    preconfig_res = await ensure_configuration(cp,{ConfigurationKey.local_pre_authorize: "false",
                                                   ConfigurationKey.authorize_remote_tx_requests: "true",
                                                   ConfigurationKey.heartbeat_interval: "0",
                                                   "AuthorizationRequired": "true",
                                                   ConfigurationKey.connection_time_out: "10"});

    if preconfig_res != 0:
        return -1

    def on_authorize(self, id_tag):
        logging.info(f"Testing got rfid tag {id_tag}")
        global expecting_new_rfid
        global new_rfid

        if expecting_new_rfid:
            new_rfid = id_tag
            expecting_new_rfid = False

        return call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted))

    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize, cp)

    if include_manual_tests:
        if await test_got_presented_rfid(cp) != True:
            return False

    if await test_remote_start(cp) != True:
        return False

    logging.info("Core profile test complete successfully")
    return True
