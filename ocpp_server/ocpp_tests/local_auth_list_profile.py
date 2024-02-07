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
    ResetStatus,
    UpdateStatus,
    UpdateType
)
from ocpp_tests.keys import key_list

from ocpp.v16.datatypes import(
    IdTagInfo,
)

from ocpp_tests.test_utils import ensure_configuration

tx_id = 1
awaiting_authorization = False

async def test_send_local_list_full(cp):
    result = await cp.call(call.SendLocalListPayload(list_version = 1, update_type = UpdateType.full, local_authorization_list = key_list))
    if result is None or result.status != UpdateStatus.accepted:
        logging.error(f"Unable to send local list in: {result}")
        return False

    while(cp.connector1_status != ChargePointStatus.preparing):
        logging.warning(f"Waiting for status preparing...({cp.connector1_status})")
        await asyncio.sleep(3)

    global awaiting_authorization
    awaiting_authorization = True

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = key_list[0]['idTag']))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted with local authorization")
        return False

    while awaiting_authorization and cp.connector1_status == ChargePointStatus.preparing:
        logging.warning(f"Waiting for state charging....{cp.connector1_status}")
        await asyncio.sleep(2)

    if awaiting_authorization == False:
        logging.error("Got an unexpected authorization request. Expected local authorization to be sufficient")
        return False

    if cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev:
        logging.error("Expected localy authorized transaction to start charging")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(tx_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of locally authorized transaction failed")
        return False

    while(cp.connector1_status != ChargePointStatus.preparing and cp.connector1_status != ChargePointStatus.finishing):
        logging.warning(f"Waiting for status preparing...({cp.connector1_status})")
        await asyncio.sleep(3)

    awaiting_authorization = True
    for entry in key_list:
        if entry['idTagInfo']['status'] == AuthorizationStatus.invalid:
            result = await cp.call(call.RemoteStartTransactionPayload(id_tag = entry['idTag']))
            if result.status != RemoteStartStopStatus.accepted:
                logging.error("Remote start transaction was not accepted with local authorization and invalid idTag")
                return False
            break

    i = 0
    while awaiting_authorization == True:
        i+=1
        if i ==5:
            logging.error("Did not get expected authorization request within timeout with invalid idTag")
            return False

        logging.warning("Waiting for authorization request")
        await asyncio.sleep(2)

    await asyncio.sleep(2)
    if cp.connector1_status != ChargePointStatus.preparing and cp.connector1_status != ChargePointStatus.finishing:
        logging.error("Unexpected state after rejected idTag with local auth")
        return False

    #TODO: test list mismatch
    return True

async def test_send_local_list_differential(cp):
    result = await cp.call(call.GetLocalListVersionPayload())
    active_list_version = result.list_version

    logging.info(f'Active local list version: {active_list_version}')
    result = await cp.call(call.SendLocalListPayload(list_version = active_list_version, update_type = UpdateType.differential, local_authorization_list = [{'idTag': key_list[0]['idTag']}]))
    if result.status != UpdateStatus.version_mismatch:
        logging.error("Differential update with version mismatch was not detected")
        return False

    result = await cp.call(call.SendLocalListPayload(list_version = active_list_version+1, update_type = UpdateType.differential, local_authorization_list = [{'idTag': key_list[0]['idTag']}]))
    if result.status != UpdateStatus.accepted:
        logging.error(f"Differential update was was not accepted: {result}")
        return False

    global awaiting_authorization
    awaiting_authorization = True

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = key_list[0]['idTag']))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted with local authorization")
        return False

    i = 0
    while awaiting_authorization:
        i+=1
        if i > 5:
            logging.error("Did not get authorization request within expected timeout after deleting local entry")
            return False

        logging.warning(f"Waiting for authorization....")
        await asyncio.sleep(2)

    while cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev:
        logging.warning(f"Awaiting charging...{cp.connector1_status}")
        await asyncio.sleep(2)

    result = await cp.call(call.RemoteStopTransactionPayload(tx_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of locally authorized transaction after differential failed")
        return False

    result = await cp.call(call.SendLocalListPayload(list_version = active_list_version+2, update_type = UpdateType.differential, local_authorization_list = [key_list[0]]))
    if result.status != UpdateStatus.accepted:
        logging.error(f"Differential update was was not accepted: {result}")
        return False

    result = await cp.call(call.GetLocalListVersionPayload())
    if result.list_version != active_list_version+2:
        logging.error(f"List version after differential update is not as expected: expected '{active_list_version+2}' got '{result.list_version}'")
        return False

    try:
        result = await cp.call(call.SendLocalListPayload(list_version = 0, update_type = UpdateType.differential, local_authorization_list = [{'idTag': key_list[0]['idTag']}]))
        if result.status != UpdateStatus.failed:
            logging.error("Differential update with version 0 did not fail")
            return False
    except Exception as e:
        logging.warn(f'differential local list update got expected Exception {e}')

    while cp.connector1_status != ChargePointStatus.preparing and cp.connector1_status != ChargePointStatus.finishing:
        logging.warning(f"Waiting for state charging....{cp.connector1_status}")
        await asyncio.sleep(2)

    awaiting_authorization = True
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = key_list[0]['idTag']))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted with local authorization after removal and reinsertion")
        return False

    while awaiting_authorization and (cp.connector1_status == ChargePointStatus.preparing or cp.connector1_status == ChargePointStatus.finishing):
        logging.warning(f"Waiting for state charging....{cp.connector1_status}")
        await asyncio.sleep(2)

    if awaiting_authorization != True:
        logging.error(f"Got unexpected authorization after removal and reinsertion of local auth idTag")
        return False

    while cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev:
        logging.warning(f"Awaiting charging...{cp.connector1_status}")
        await asyncio.sleep(2)

    logging.info(f'Should be charging or suspended_ {cp.connector1_status}')
    result = await cp.call(call.RemoteStopTransactionPayload(tx_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of locally authorized transaction after removal and reinsertion failed")
        return False

    return True

async def test_clear_local_list(cp):
    result = await cp.call(call.SendLocalListPayload(list_version = 1, update_type = UpdateType.full, local_authorization_list = None))
    if result is None or result.status != UpdateStatus.accepted:
        logging.error(f"Unable to clear local list: {result}")
        return False

    result = await cp.call(call.GetLocalListVersionPayload())
    if result.list_version != 0:
        logging.error("List version was not cleared after list clear")
        return False

    return True

async def test_local_auth_list_profile(cp, include_manual_tests = True):
    logging.info('Setting up loacal auth list profile test')
    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.local_pre_authorize: "true",
                                                    ConfigurationKey.local_auth_list_enabled: "true",
                                                    ConfigurationKey.authorization_cache_enabled: "false",
                                                    ConfigurationKey.authorize_remote_tx_requests: "true",
                                                    ConfigurationKey.heartbeat_interval: "0",
                                                    ConfigurationKey.clock_aligned_data_interval: "0",
                                                    ConfigurationKey.meter_value_sample_interval: "0",
                                                    ConfigurationKey.connection_time_out: "5",
                                                    "AuthorizationRequired": "true"})

    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info(f"Local list testing got start transaction with id {id_tag}")
        info=dict(parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        global tx_id
        tx_id += 1

        for entry in key_list:
            if entry['idTag'] == id_tag:
                return call_result.StartTransactionPayload(
                    id_tag_info=entry['idTagInfo'],
                    transaction_id=tx_id
                )

        return call_result.StartTransactionPayload(
                    id_tag_info=dict(status=AuthorizationStatus.Invalid),
                    transaction_id=tx_id
                )

    def on_authorize_request(self, id_tag):
        logging.info(f"Local list testing got auth request with id {id_tag}")
        global awaiting_authorization
        awaiting_authorization = False

        for entry in key_list:
            if entry['idTag'] == id_tag:
                return call_result.AuthorizePayload(entry['idTagInfo'])

        return call_result.AuthorizePayload(dict(status=AuthorizationStatus.invalid))

    cp.route_map[Action.StartTransaction]["_on_action"] = types.MethodType(on_start_transaction, cp)
    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize_request, cp)

    if preconfig_res != 0:
        return False

    if await test_send_local_list_full(cp) != True:
        return False

    if await test_send_local_list_differential(cp) != True:
        return False

    if await test_clear_local_list(cp) != True:
        return False

    return True
