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
    ChargePointStatus,
    AuthorizationStatus,
    Action,
    ReservationStatus,
    CancelReservationStatus,
    RemoteStartStopStatus,
    ResetType,
    ResetStatus
)
from ocpp.v16.datatypes import(
    IdTagInfo,
)

from ocpp_tests.test_utils import ensure_configuration

awaiting_authorization = True
awaiting_parent = None
awaiting_auth_result = AuthorizationStatus.accepted

async def test_reserve_now_and_cancel(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.ReserveNowPayload(connector_id = 1, expiry_date = (datetime.utcnow() + timedelta(days=1)).isoformat(), id_tag="test_tag", reservation_id = 34))
    if result is None or result.status != ReservationStatus.accepted:
        logging.error(f"Reservation was not accepted in available state: {result}")
        return False

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.reserved:
        logging.error("Chargepoint status did not go to reserved after accepted ")
        return False

    result = await cp.call(call.CancelReservationPayload(reservation_id = 34))
    if result is None or result.status != CancelReservationStatus.accepted:
        logging.error(f"Cancel reservation was not accepted: {result}")
        return False

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f"Unexpected state after cancel reservation: {cp.connector1_status}")
        return False

    return True

async def test_reserve_now_and_timeout(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.ReserveNowPayload(connector_id = 1, expiry_date = (datetime.utcnow() + timedelta(seconds=10)).isoformat(), id_tag="test_tag", reservation_id = 35))
    if result is None or result.status != ReservationStatus.accepted:
        logging.error(f"Reservation was not accepted in available state: {result}")
        return False

    await asyncio.sleep(13)
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f"Reservation did not automatically cancel correctly: {cp.connector1_status}")
        return False

    return True

async def test_reserve_now_and_remote_start(cp):
    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.authorize_remote_tx_requests: "true"})
    if preconfig_res != 0:
        logging.error("Unable to prepare configuration for reserve_now_and_remote_start")

    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.ReserveNowPayload(connector_id = 1, expiry_date = (datetime.utcnow() + timedelta(days=1)).isoformat(), id_tag="test_tag", reservation_id = 34))
    if result is None or result.status != ReservationStatus.accepted:
        logging.error(f"Reservation was not accepted in available state: {result}")
        return False

    await asyncio.sleep(2)
    global awaiting_authorization
    awaiting_authorization = True

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "Other_test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in reservation state with other id_tag and AuthorizeRemote was true")
        return False

    while awaiting_authorization:
        logging.warning("Waiting for authorization 1")
        await asyncio.sleep(2)

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.reserved:
        logging.error("Did not stay in reserved after other remote start idToken")
        return False

    awaiting_authorization = True
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in reservation state with same id_tag")
        return False

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.preparing:
        logging.error("Did not transition to preparing after correct reserved token accepted")
        return False

    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.authorize_remote_tx_requests: "false"})
    if preconfig_res != 0:
        logging.error("Unable to change configuration for reserve_now_and_remote_start")

    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.ReserveNowPayload(connector_id = 1, expiry_date = (datetime.utcnow() + timedelta(days=1)).isoformat(), id_tag="test_tag", reservation_id = 36))
    if result is None or result.status != ReservationStatus.accepted:
        logging.error(f"Reservation was not accepted in available state: {result}")
        return False

    await asyncio.sleep(2)
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "Other_test_tag"))
    if result.status != RemoteStartStopStatus.rejected:
        logging.error(f"Remote start transaction was not rejected while in reservation state with other id_tag and AuthorizeRemote was false: {result.status}")
        return False

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.reserved:
        logging.error("Did not stay in reserved after other remote start idToken")
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in reservation state with same id_tag")
        return False

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.preparing:
        logging.error("Did not transition to preparing after correct reserved token accepted")
        return False

    return True

async def test_reserve_now_and_reset(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.ReserveNowPayload(connector_id = 1, expiry_date = (datetime.utcnow() + timedelta(days=1)).isoformat(), id_tag="test_tag", reservation_id = 37))
    if result is None or result.status != ReservationStatus.accepted:
        logging.error(f"Reservation was not accepted in available state: {result}")
        return False

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.reserved:
        logging.error("Chargepoint did not enter reserved to test persistence through boot")
        return False

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test boot notification")
        return False

    while(cp.connector1_status == ChargePointStatus.unavailable):
        logging.warning(f"Waiting for status !unavailable...({cp.connector1_status})")
        await asyncio.sleep(3)

    if cp.connector1_status != ChargePointStatus.reserved:
        logging.error("Reservation did not persistence boot")
        return False

    result = await cp.call(call.CancelReservationPayload(reservation_id = 37))
    if result is None or result.status != CancelReservationStatus.accepted:
        logging.error(f"Cancel reservation was not accepted after reset: {result}")
        return False

    return True

async def test_reservation_profile(cp, include_manual_tests = True):
    logging.info('Setting up reservation profile test')
    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.local_pre_authorize: "false",
                                                    ConfigurationKey.authorize_remote_tx_requests: "true",
                                                    ConfigurationKey.heartbeat_interval: "0",
                                                    ConfigurationKey.clock_aligned_data_interval: "0",
                                                    ConfigurationKey.meter_value_sample_interval: "0",
                                                    ConfigurationKey.connection_time_out: "5",
                                                    "AuthorizationRequired": "true"})

    if preconfig_res != 0:
        return False

    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info(f"Reservation testing got start transaction with id {id_tag}")
        info=dict(parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        global tx_id
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=tx_id
        )

    def on_authorize_request(self, id_tag):
        global awaiting_auth_result
        global awaiting_parent
        global awaiting_authorization
        awaiting_authorization = False

        logging.info(f'Reservation test {awaiting_auth_result} {id_tag} with parent: {awaiting_parent}')
        return call_result.AuthorizePayload(
            dict(expiry_date = (datetime.utcnow() + timedelta(days=1)).isoformat(), parentIdTag=awaiting_parent, status=awaiting_auth_result)
        )

    cp.route_map[Action.StartTransaction]["_on_action"] = types.MethodType(on_start_transaction, cp)
    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize_request, cp)

    if include_manual_tests:
        if await test_reserve_now_and_cancel(cp) != True:
            return False

        if await test_reserve_now_and_timeout(cp) != True:
            return False

        if await test_reserve_now_and_remote_start(cp) != True:
            return False

        if await test_reserve_now_and_reset(cp) != True:
            return False

    logging.info("Smart charging test complete successfully")
    return True
