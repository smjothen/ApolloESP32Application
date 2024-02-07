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
    ChargingProfilePurposeType,
    ChargingProfileKindType,
    ChargingRateUnitType,
    ChargePointStatus,
    RemoteStartStopStatus,
    Measurand,
    ClearChargingProfileStatus,
    ChargingProfileStatus,
    UnlockStatus
)
from ocpp.v16.datatypes import(
    ChargingProfile,
    IdTagInfo,
    ChargingSchedule,
    ChargingSchedulePeriod
)
from ocpp.exceptions import GenericError
from ocpp_tests.test_utils import ensure_configuration

tx_id = 345
auth_tag = "test"

awaiting_value = 0
awaiting_meter_value = False

def static_vars(**kwargs):
    def decorate(func):
        for k in kwargs:
            setattr(func, k, kwargs[k])
        return func
    return decorate

@static_vars(profile_id = 1267086205)
def create_charging_profile(transaction_id = None, stack_level = 0, purpose = ChargingProfilePurposeType.tx_profile,
                            kind = ChargingProfileKindType.absolute, schedule = None, limit = 16):

    create_charging_profile.profile_id += 1

    tmp_schedule = schedule
    if tmp_schedule is None:
        tmp_schedule = ChargingSchedule(
            charging_rate_unit = ChargingRateUnitType.amps,
            charging_schedule_period = [
                ChargingSchedulePeriod(
                    start_period = 0,
                    limit = limit,
                    number_phases = 3
                )
            ]
        )

    profile_result = ChargingProfile(
        charging_profile_id = create_charging_profile.profile_id,
        transaction_id = transaction_id,
        stack_level = stack_level,
        charging_profile_purpose = purpose,
        charging_profile_kind = kind,
        charging_schedule = tmp_schedule
    )


    if (profile_result.charging_profile_kind == ChargingProfileKindType.absolute or
        profile_result.charging_profile_kind == ChargingProfileKindType.recurring):

        profile_result.charging_schedule.start_schedule = datetime.utcnow().isoformat() + 'Z'

    logging.info(f'Created charging profile: {profile_result}')
    return profile_result

""" OCPP specification says "It is not possible to set a ChargingProfile with purpose set to TxProfile without presence
of an active transaction, or in advance of a transaction"

:param cp: chargepoint

:return true if test completes with expected results. false otherwise.
"""
async def test_tx_profile_outside_transaction(cp):
    while(cp.connector1_status != ChargePointStatus.available):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id)), suppress=False)

    if result is not None and (result.status == ChargingProfileStatus.rejected):
        return True
    else:
        logging.error(f'Failed tx outside transaction result: {result}')
        return False

async def test_tx_profile_within_transaction(cp):
    while(cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev):
        logging.warning(f"Waiting for status charging...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id)))
    if result is not None and result.status == ChargingProfileStatus.accepted:
        return True
    else:
        logging.error(f'Failed tx within transaction result: {result}')
        return False

async def test_tx_profile_applied_16A_0A(cp):
    while(cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev):
        logging.warning(f"Waiting for status charging...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=16)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.info('Unable to set charging profile with limit 16')
        return False

    global awaiting_value
    awaiting_value= 16
    global awaiting_meter_value
    awaiting_meter_value= True

    i = 0
    while(awaiting_meter_value):
        await asyncio.sleep(1)
        if i > 60:
            logging.error('Did not get expected meter value (A16) within timeout')
            return False
        i+=1

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=0)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.info('Unable to set charging profile with limit 0')
        return False

    awaiting_value= 0
    awaiting_meter_value= True

    i = 0
    while(awaiting_meter_value):
        await asyncio.sleep(1)
        if i > 60:
            logging.error('Did not get expected meter value (0A) within timeout')
            return False
        i+=1

    if cp.connector1_status != ChargePointStatus.suspended_evse:
        logging.error('Got expected meter values, but not expected chargepoint state')
        return False
    else:
        logging.info('Transition from 16 - 0 worked as expected')
        return True

async def test_tx_profile_applied_current_at_iec_61851_limits(cp):
    while(cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev and cp.connector1_status != ChargePointStatus.suspended_evse):
        logging.warning(f"Waiting for status charging...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=6)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.info('Unable to set charging profile with limit 6')
        return False

    global awaiting_value
    awaiting_value= 6
    global awaiting_meter_value
    awaiting_meter_value= True

    i = 0
    while(awaiting_meter_value):
        await asyncio.sleep(1)
        if i > 60:
            logging.error('Did not get expected meter value (6A) within timeout')
            return False
        i+=1

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=5)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.error('Unable to set charging profile with limit 5A')
        return False

    awaiting_value= 0
    awaiting_meter_value= True

    i = 0
    while(awaiting_meter_value):
        await asyncio.sleep(1)
        if i > 60:
            logging.error('Did not get expected meter value (0A when set to 5A) within timeout')
            return False
        i+=1

    if cp.connector1_status != ChargePointStatus.suspended_evse:
        logging.error('Got expected meter values, but not expected chargepoint state')
        return False

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=80)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.error('Unable to set charging profile with limit 80A')
        return False

    awaiting_value= 32 # Expecting highest value allowed by Go. TODO: make this part of the test more robust as other limits may apply
    awaiting_meter_value= True

    i = 0
    while(awaiting_meter_value):
        await asyncio.sleep(1)
        if i > 60:
            logging.error('Did not get expected meter value (32A when set to 80A) within timeout')
            return False
        i+=1

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=81)))
    if result is None:
        logging.info('Unable to set charging profile with limit 81A')
        return True
    else:
        logging.error(f'Unexpected result: {result}')
        return False

    return False

async def test_tx_profile_overcurrent(cp):
    while(cp.connector1_status != ChargePointStatus.charging):
        logging.warning(f"Waiting for status charging with load...({cp.connector1_status})")
        await asyncio.sleep(3)

    result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = create_charging_profile(transaction_id = tx_id, limit=6)))
    if result is None or result.status != ChargingProfileStatus.accepted:
        logging.info('Unable to set charging profile with limit 6')
        return False

    i = 0
    while cp.connector1_status == ChargePointStatus.charging:
        i+=1
        if i > 15:
            logging.error("Overcurrent did not trigger within expected delay")
            return False

        logging.warning("Waiting for overcurrent to trigger state change to faulted")
        await asyncio.sleep(4)

    if cp.connector1_status != ChargePointStatus.faulted:
        logging.error(f'Overcurrent did not result in faulted state: {cp.connector1_status}')
        return False

    result = await cp.call(call.UnlockConnectorPayload(connector_id = 1))
    if result.status != UnlockStatus.unlocked:
        logging.error(f'Unable to unlock connector while in state preparing')
        return False

    i = 0
    while cp.connector1_status == ChargePointStatus.faulted:
        i+=1
        if i > 5:
            logging.error("Unlock connector in overcurrent faulted state did not result in transition away from faulted")
            return False

        logging.info(f'waiting for unlock command to take effect... {cp.connector1_status}')
        await asyncio.sleep(3)

    await asyncio.sleep(4) # should transition back to charging, then to finishing and then to available
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f'Expected charger to enter available after disconnect command')
        return False

    return True

async def test_smart_charging_profile(cp, include_manual_tests = True):
    logging.info('Setting up smart charging profile test')
    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.local_pre_authorize: "false",
                                                    ConfigurationKey.authorize_remote_tx_requests: "true",
                                                    ConfigurationKey.heartbeat_interval: "0",
                                                    ConfigurationKey.clock_aligned_data_interval: "0",
                                                    ConfigurationKey.meter_value_sample_interval: "10",
                                                    ConfigurationKey.meter_values_sampled_data:
                                                    Measurand.current_offered + "," + Measurand.current_import,
                                                    "AuthorizationRequired": "false"})

    if preconfig_res != 0:
        return False

    result = await cp.call(call.ClearChargingProfilePayload())
    if result is None:
        logging.error('Unable to clear charging profiles to prepare for smart charging tests')
        return False

    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info(f"Testing got start transaction with id {id_tag}")
        info=dict(parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        global tx_id
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=tx_id
        )

    def on_meter_values(self, call_unique_id, connector_id, meter_value, transaction_id, **kwargs):
        global awaiting_meter_value
        logging.info(f'Got meter values')
        if awaiting_meter_value and meter_value is not None:
            for m_value in meter_value:
                for sample in m_value['sampled_value']:
                    if sample['measurand'] == Measurand.current_offered:
                        if int(float(sample['value'])) == awaiting_value:
                            logging.info(f'Got awaiting meter value {awaiting_value}A')
                            awaiting_meter_value = False
                            return call_result.MeterValuesPayload()
                        else:
                            logging.info(f'Expected {awaiting_value} got {sample["value"]}')
        return call_result.MeterValuesPayload()

    cp.route_map[Action.StartTransaction]["_on_action"] = types.MethodType(on_start_transaction, cp)
    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_values, cp)


    if include_manual_tests:
        if await test_tx_profile_outside_transaction(cp) != True:
            return False

        if await test_tx_profile_within_transaction(cp) != True:
            return False

        if await test_tx_profile_applied_16A_0A(cp) != True:
            return False

        if await test_tx_profile_applied_current_at_iec_61851_limits(cp) != True:
            return False

        if await test_tx_profile_overcurrent(cp) != True:
            return False

    logging.info("Smart charging test complete successfully")
    return True
