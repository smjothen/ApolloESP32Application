#!/usr/bin/env python3
from collections import deque
import types
import asyncio
#import serial_asyncio
import logging
from datetime import datetime, timedelta
import dateutil
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
    Phase,
    Reason
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import (
    ensure_configuration,
    ZapClientOutput,
    ZapClientInput,
    OperationState,
    MCUWarning,
    wait_for_cp_status,
    wait_for_cp_action_event
)
from ocpp_tests.keys import key_list

awaiting_meter_connectors = []
new_meter_values = dict()
last_meter_value_timestamps = deque(maxlen = 10)

async def test_got_presented_rfid(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.Authorize: asyncio.Event()
    }

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f"CP did not enter available to test presented RFID tag")
        return False

    cp.additional_keys = [{
        'idTag': '*',
        'idTagInfo': IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat())
    }]

    logging.warning("Present RFID tag...")
    try:
        await wait_for_cp_action_event(cp, Action.Authorize)
    except Exception as e:
        logging.error(f"Failed when waiting for RFID tag: {e}")
        return False

    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, f'does "{cp.last_auth_tag}" match presented id? y/n ')

    if response.lower() != "y":
        logging.error(f'Authorize got incorrect RFID. User typed {response}')
        return False

    response = await loop.run_in_executor(None, input, f'Did the CP indicate accepted via LED and sound? y/n ')
    if response.lower() != "y":
        logging.error(f'Unexpected LED: {response}')
        return False

    logging.warning("Present RFID tag again and set car in charging state")
    state = await wait_for_cp_status(cp, [ChargePointStatus.charging])
    if state != ChargePointStatus.charging:
        logging.error("CP did not enter charging to test locally started transaction")
        return False

    logging.warning("Present same RFID tag again to check that it can stop the transaction")
    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing])
    if state != ChargePointStatus.finishing:
        logging.error("CP did not enter finishing when testing locally stopped transaction")
        return False

    response = await loop.run_in_executor(None, input, f'Did the CP react to presented tag as expected? y/n ')
    if response.lower() != "y":
        logging.error(f'Unexpected stopping behaviour: {response}')
        return False

    return True

async def test_remote_start(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.Authorize: asyncio.Event()
    }

    conf_result = await ensure_configuration(cp, {ConfigurationKey.connection_time_out: "7"})
    if conf_result != 0:
        logging.error(f'Unable to configure longer connection timeout: {conf_result}')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if state != ChargePointStatus.available:
        logging.error("CP did not enter available to start remote_start_test")
        return False

    cp.additional_keys = [
        {
            'idTag': 'test_tag_accept',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat())
        },
        {
            'idTag': 'test_tag_concurrent',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.concurrent_tx)
        },
        {
            'idTag': 'test_tag_blocked',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.blocked)
        },
        {
            'idTag': 'test_tag_invalid',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.invalid)
        },
        {
            'idTag': 'test_tag_expired',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.expired)
        },
        {
            # accepted but actually expired
            'idTag': 'test_tag_expired2',
            'idTagInfo': IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() - timedelta(days=1)).isoformat())
        }
    ]

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = 'test_tag_accept'))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in available state")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.Authorize, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for Authorize of remote start with valid tag: {e}")
        return False

    timeout_start = time.time()

    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing], 10)
    if state != ChargePointStatus.preparing:
        logging.error(f"CP did not enter preparing after accepted Authorize.req: {cp.connector1_status}")
        return False

    if cp.last_auth_tag != 'test_tag_accept':
        logging.error(f"Unexpected rfid tag in Authorize.req :{cp.last_auth_tag}")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 16)
    if state != ChargePointStatus.available:
        logging.error("CP did not enter available after connection timeout")
        return False

    if (time.time() - timeout_start) < 7:
        logging.error(f"CP did not wait for Connection timeout. Waited for less than {int(time.time() - timeout_start)}")
        return False

    # We change connection timeout to ensure that CP does not incorrectly go back to available due to
    # timeout instead of non-accepted idTag. It is also used later to test that accept idTag waits for
    # configured connection timeout.
    conf_result = await ensure_configuration(cp, {ConfigurationKey.connection_time_out: "30"})
    if conf_result != 0:
        logging.error(f'Unable to configure longer connection timeout: {conf_result}')
        return False

    for key in cp.additional_keys[1:]:
        logging.info(f"Testing that {key['idTag']} is rejected")
        result = await cp.call(call.RemoteStartTransactionPayload(id_tag = key['idTag']))
        if result.status != RemoteStartStopStatus.accepted:
            logging.error(f"Remote start transaction was not accepted with {key['idTag']} in available state")
            return False

        try:
            await wait_for_cp_action_event(cp, Action.Authorize, 10)
        except Exception as e:
            logging.error(f"Failed when waiting for Authorize of remote start with {key['idTag']} tx tag: {e}")
            return False

        # When authorizing the CP sets pending_ocpp_authorize to true in sessionHandler.c and sends the Authorize.req.
        # pending_ocpp_autorize is checked on the next loop in sessionHandler.c to set ocpp state to Preparing or
        # Available. If the Authorize.conf indicate that the idTag is not allowed and is recieved before this next
        # loop, then the CP may not enter (or send) status Preparing. This is arguably incorrect as authorization
        # is part of preparation. We could argue that it is a "minimal status duration for certain status transitions
        # separate of the MinimumStatusDuration" which the spec allows. The CS should therefore not require this
        # notification. The commented test below should be re-enabled if Preparation status is deemed as necessary.
        # The duration of the wait should then also be increased.

        state = await wait_for_cp_status(cp, [ChargePointStatus.preparing], 2)
        # if state != ChargePointStatus.preparing:
        #     logging.error(f"CP did not enter preparing after {key['idTag']} Authorize.req: {cp.connector1_status}")
        #     return False

        if cp.last_auth_tag != key['idTag']:
            logging.error(f"Unexpected rfid tag in Authorize.req. Expected {key['idTag']} but got {cp.last_auth_tag}")
            return False

        state = await wait_for_cp_status(cp, [ChargePointStatus.available], 9)
        if state != ChargePointStatus.available:
            logging.error(f"CP did not remain in available after {key['idTag']} Authorize.req: {cp.connector1_status}")
            return False

    timeout_start = time.time()
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = 'test_tag_accept'))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in available state")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.Authorize, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for Authorize of remote start with test_tag_accept: {e}")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing], 10)
    if state != ChargePointStatus.preparing:
        logging.error('Did not get transition to preparing')
        return False

    if cp.last_auth_tag != 'test_tag_accept':
        logging.error(f"Unexpected rfid tag in Authorize.req :{new_rfid}")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 40)
    if state != ChargePointStatus.available:
        logging.error("CP did not go back to available after longer connection timeout")
        return False

    if (time.time() - timeout_start) < 30:
        logging.error(f'longer connection timout was not long')
        return False

    logging.info("Set car into state B")
    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing])
    while cp.connector1_status != ChargePointStatus.preparing:
        logging.error("Car did not connect to test remote start during preparing")
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = 'test_tag_accept'))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in preparing state")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse], 16)
    if state not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse]:
        logging.error("CP did not enter state charging after all preconditions where met")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(cp.last_transaction_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of remote authorized transaction failed")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing], 16)
    if state != ChargePointStatus.finishing:
        logging.error("CP did not enter state finishing after transaction was stopped remotely")
        return Falsea

    logging.warning("Waiting to see if finishing remains")
    await asyncio.sleep(5)
    if cp.connector1_status != ChargePointStatus.finishing:
        logging.error("CP did not remain in finishing state")
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag_accept"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in finishing state")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse], 16)
    if state not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse]:
        logging.error("CP did not enter state charging after all preconditions where met in finishing state")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(cp.last_transaction_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of remote authorized transaction failed")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing], 16)
    if state != ChargePointStatus.finishing:
        logging.error("CP did not enter state finishing after transaction was stopped remotely")
        return False

    logging.warning("Waiting to see if finishing remains")
    await asyncio.sleep(5)
    if cp.connector1_status != ChargePointStatus.finishing:
        logging.error("CP did not remain in finishing state")
        return False

    return True

async def test_meter_values(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
    }

    result = await ensure_configuration(cp, {"AuthorizationRequired": "false",
                                             'DefaultIdToken': 'test_tag_non'})
    cp.additional_keys = [{
        'idTag': 'test_tag_non',
        'idTagInfo': dict(expiry_date = (datetime.utcnow() + timedelta(hours=1)).isoformat(), status='Accepted')
    }]

    if result != 0:
        logging.error("Unable to configure AuthorizationRequired to test meter values")
        return False

    result = await cp.call(call.GetConfigurationPayload([ConfigurationKey.connector_phase_rotation]))
    if result == None or result.configuration_key == None or len(result.configuration_key) != 1:
        logging.error("Unable to get phase rotation configuration")
        return False

    if "value" not in result.configuration_key[0] or "Unknown" in result.configuration_key[0]["value"]:
        logging.error("Configured phase rotation value does not indicate single or three phase")
        return False

    is_single_phase = "NotApplicable" in result.configuration_key[0]["value"]

    accept_measurand = [Measurand.current_import, Measurand.current_offered, Measurand.energy_active_import_register,
                        Measurand.energy_active_import_interval, Measurand.power_active_import, Measurand.temperature, Measurand.voltage]

    reject_measurands = [measurand for measurand in list(Measurand) if measurand not in accept_measurand]

    for measurand in accept_measurand:
        change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_aligned_data, measurand))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to configure aligned data to {measurand}")
            return False

        change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_sampled_data, measurand))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to configure sampled data to {measurand}")
            return False

    for measurand in reject_measurands:
        change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_aligned_data, measurand))
        if(result == None or change_res.status != ConfigurationStatus.rejected):
            logging.error(f"Unexpected result when configure aligned data with not supported {measurand}")
            return False

        change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_sampled_data, measurand))
        if(result == None or change_res.status != ConfigurationStatus.rejected):
            logging.error(f"Unexpected result when configure sampled data with not supported {measurand}")
            return False

    complete_measurand = ','.join(accept_measurand)
    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_aligned_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to configure aligned data to {complete_measurand}")
        return False

    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_sampled_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to configure sampled data to {complete_measurand}")
        return False

    for interval_type in [ConfigurationKey.clock_aligned_data_interval, ConfigurationKey.meter_value_sample_interval]:
        res = await cp.call(call.ChangeConfigurationPayload(interval_type, "3"))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to configure {interval_type} with {complete_measurand}")
            return False

        wanted_state = [ChargePointStatus.available, ChargePointStatus.preparing] if interval_type is ConfigurationKey.clock_aligned_data_interval else [ChargePointStatus.charging, ChargePointStatus.suspended_evse, ChargePointStatus.suspended_ev]
        state = await wait_for_cp_status(cp, wanted_state)
        if state not in wanted_state:
            logging.warning(f"Failed while waiting for state when testing meter values with {interval_type}")
            return False

        global awaiting_meter_connectors
        connectors_to_check = [0,1] if interval_type is ConfigurationKey.clock_aligned_data_interval else [1]
        awaiting_meter_connectors = connectors_to_check.copy()

        i = 0
        while len(awaiting_meter_connectors) > 0:
            logging.warning(f"Awaiting new meter values for connector {awaiting_meter_connectors}")

            if i > 3:
                logging.error("Did not get expected meter values within timeout")
                return False

            await asyncio.sleep(3)

        logging.info("Waiting 40 sec to get meter values to check correct interval")
        await asyncio.sleep(40)

        start_end_time_diff = (last_meter_value_timestamps[-1] - last_meter_value_timestamps[0]).total_seconds()
        if start_end_time_diff > 33 or start_end_time_diff < 27:
            logging.error(f'Expected {interval_type} of 3 sec to send last 10 messages within 27 to 33 sec but got them within {start_end_time_diff} sec')
            return False

        res = await cp.call(call.ChangeConfigurationPayload(interval_type, "6"))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to change {interval_type} to 6 sec")
            return False

        logging.info("Waiting 70 sec to get meter values to check new interval")
        await asyncio.sleep(70)

        start_end_time_diff = (last_meter_value_timestamps[-1] - last_meter_value_timestamps[0]).total_seconds()
        if start_end_time_diff > 66 or start_end_time_diff < 54:
            logging.error(f'Expected {interval_type} of 6 sec to send last 10 messages within 54 to 66 sec but got them within {start_end_time_diff} sec')
            return False

        res = await cp.call(call.ChangeConfigurationPayload(interval_type, "0"))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to turn off {interval_type}")
            return False

        gotten_phases = set()
        for connector in connectors_to_check:
            got_measurands = set()
            for value in new_meter_values[connector]:
                logging.info(f"value: {value}")
                for sample in value['sampled_value']:
                    got_measurands.add(sample['measurand'])

                    if "phase" in sample:
                        gotten_phases.add(sample["phase"])

        if is_single_phase and (Phase.l1 not in gotten_phases or Phase.l2 in gotten_phases or Phase.l3 in gotten_phases):
            logging.error("Single phase charger defaulted to report non L1 phase meter value")
            return False

        if not is_single_phase and len(gotten_phases) != 3:
            logging.error("three phase charger did not default to report all phases")
            return False

        got_all_values = True
        for measurand in accept_measurand:
            if measurand not in got_measurands:
                logging.error(f'Did not get measurand {measurand} for connector {connector}')
                got_all_values = False

        if not got_all_values:
            return False

    for i in range(len(accept_measurand)):
        accept_measurand[i] = accept_measurand[i] + ".L" + str(1 + i % 3)

    complete_measurand = ','.join(accept_measurand)

    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_aligned_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.rejected):
        logging.error(f"Should not be able to specify phase on unsupported measurand but clock aligned config accepts: {complete_measurand}")
        return False

    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_sampled_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.rejected):
        logging.error(f"Should not be able to specify phase on unsupported measurand but sampled config accepts: {complete_measurand}")
        return False

    complete_measurand = ','.join([Measurand.current_import + "." + Phase.l1, Measurand.temperature + "." + Phase.l2, Measurand.voltage + "." + Phase.l3])

    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_sampled_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to request specific phase for sampled meter value with: {complete_measurand}")
        return False

    change_res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.meter_values_aligned_data, complete_measurand))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to request specific phase for aligned meter value with: {complete_measurand}")
        return False

    res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.clock_aligned_data_interval, "3"))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to configure clock aligned interval")
        return False


    state = await wait_for_cp_status(cp, [ChargePointStatus.available, ChargePointStatus.preparing])
    if state not in [ChargePointStatus.available, ChargePointStatus.preparing]:
        logging.error("CP did not enter state available or preparing")
        return False

    connectors_to_check = [0,1]
    awaiting_meter_connectors = connectors_to_check.copy()

    i = 0
    while len(awaiting_meter_connectors) > 0:
        logging.warning(f"Awaiting new meter values for connector {awaiting_meter_connectors}")

        if i > 3:
            logging.error("Did not get expected meter values within timeout")
            return False

        await asyncio.sleep(3)

    for connector in [0,1]:
        got_measurands = set()
        for value in new_meter_values[connector]:
            logging.info(f"value: {value}")
            for sample in value['sampled_value']:
                if ((sample['measurand'] == Measurand.current_import and 'phase' in sample and sample['phase'] == Phase.l1)
                    or (sample['measurand'] == Measurand.temperature and 'phase' in sample and sample['phase'] == Phase.l2)
                    or (sample['measurand'] == Measurand.voltage and 'phase' in sample and sample['phase'] == Phase.l3)):
                        got_measurands.add(sample['measurand'])
                else:
                    logging.error(f"Got Unexpected measurand and phase combination in {sample}")
                    return False

        if connector == 0: # Some phases are only reported on connector 1
            if Measurand.temperature in got_measurands:
                logging.error(f"Did not expect phase related temperature value on connector 0")
                return False
            got_measurands.add(Measurand.temperature)

        if len(got_measurands) != 3:
            logging.error(f"Did not get the three expected measurands. Got {got_measurands} ({len(got_measurands)})")
            return False

    res = await cp.call(call.ChangeConfigurationPayload(ConfigurationKey.clock_aligned_data_interval, "0"))
    if(result == None or change_res.status != ConfigurationStatus.accepted):
        logging.error(f"Unable to disable clock aligned interval after test")
        return False

    return True

async def test_boot_notification_and_non_accepted_state(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.BootNotification: asyncio.Event(),
        Action.Heartbeat: asyncio.Event()
    }

    state = await wait_for_cp_status(cp, ChargePointStatus.available)
    if state != ChargePointStatus.available:
        logging.warning(f"Failed while waiting for state when testing meter values with {interval_type}")
        return False

    cp.boot_timestamp = None
    cp.boot_interval = 15
    cp.boot_status = RegistrationStatus.accepted

    cp.connector1_status = ChargePointStatus.unavailable

    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test boot notification")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 120)
    except Exception as e:
        logging.error(f"Failed when waiting for BootNotification with {cp.boot_timestamp}, {cp.boot_interval} and {cp.boot_status}: {e}")
        return False

    non_unavailable_status = [x for x in ChargePointStatus if x != ChargePointStatus.unavailable]
    state = await wait_for_cp_status(cp, non_unavailable_status, 16)
    if state == ChargePointStatus.unavailable:
        logging.error(f"CP did not exit unavailable after boot {state}")
        return False

    # Wait for heartbeat to ensure that there are no messages in queue
    try:
        await wait_for_cp_action_event(cp, Action.Heartbeat, 60)
    except Exception as e:
        logging.error(f"Failed when waiting for first Heartbeat")
        return False

    #TODO: find a way to detect messages that would replace heartbeat (like status notification with weakSignal)
    last_message = time.time()
    try:
        await wait_for_cp_action_event(cp, Action.Heartbeat, 20)
    except Exception as e:
        logging.error(f"Failed when waiting for second Heartbeat")
        return False

    new_message = time.time()
    if (new_message - last_message) < (cp.boot_interval -1):
        logging.error(f"Heartbeat interval is not same as requested in boot notification. Was ca: {new_message - last_message} sec")
        return False

    # Attempt boot trigger as it should not be allowed while accepted
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.boot_notification))
    if result.status != TriggerMessageStatus.rejected:
        logging.error(f"Trigger was not rejected as suggested by errata v4.0. Got: {result}")
        return False

    cp.boot_interval = 10
    cp.boot_status = RegistrationStatus.pending

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test pending boot notification")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 120)
    except Exception as e:
        logging.error(f"Failed when waiting for BootNotification with {cp.boot_timestamp}, {cp.boot_interval} and {cp.boot_status}: {e}")
        return False

    last_message = time.time() # last bootNotification.req

    config_result = await ensure_configuration(cp, {ConfigurationKey.heartbeat_interval: "30"})
    if config_result != 0:
        logging.error("Unable to configure charger in pending state")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, cp.boot_interval + 5)
    except Exception as e:
        logging.error(f"Failed when waiting for timeout BootNotification with {cp.boot_timestamp}, {cp.boot_interval} and {cp.boot_status}: {e}")
        return False

    new_message = time.time()
    if (new_message - last_message) < (cp.boot_interval -1):
        logging.error(f"Boot interval is not same as requested in boot notification. Was ca: {new_message - last_message} sec")
        return False

    cp.boot_interval = 0
    cp.boot_status = RegistrationStatus.rejected

    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.boot_notification))
    if result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger was not accepted during pending. Got: {result}")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for triggered BootNotification with {cp.boot_timestamp}, {cp.boot_interval} and {cp.boot_status}: {e}")
        return False

    logging.warning("Waiting for message timeout and default retry interval (Expect significat delay (30 sec then 90 sec))")
    config_result = await ensure_configuration(cp, {ConfigurationKey.heartbeat_interval: 30})
    if config_result == 0:
        logging.error("Was able to incorrectly configure charger while rejected")
        return False

    cp.boot_status = RegistrationStatus.accepted
    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 120)
    except Exception as e:
        logging.error(f"Failed when waiting for timeout BootNotification with {cp.boot_timestamp}, {cp.boot_interval} and {cp.boot_status}: {e}")
        return False

    return True

async def test_get_and_set_configuration(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.BootNotification: asyncio.Event()
    }

    cp.boot_timestamp = None
    cp.boot_interval = 0
    cp.boot_status = RegistrationStatus.accepted

    result = await cp.call(call.GetConfigurationPayload(None))
    if(result == None):
        logging.info("GetConfiguration for all values failed")
        return False

    if len(result.configuration_key) < 40 or len(result.configuration_key) > 60:
        logging.info(f'Unexpected number of configuration keys: {len(result.configuration_key)}')
        return False

    if result.unknown_key != None and len(result.unknown_key) != 0:
        logging.info(f'Got unknown keys when requesting all configuration keys')
        return False

    for key in result.configuration_key:
        if ConfigurationKey.allow_offline_tx_for_unknown_id not in ConfigurationKey:
            logging.error(f'Did not expect configuration key: {key}')
            return False

    connection_time_out_key = [x for x in result.configuration_key if x['key'] == ConfigurationKey.connection_time_out]
    if len(connection_time_out_key) != 1:
        logging.error(f"Expected one entry with {ConfigurationKey.connection_time_out} got: {connection_time_out_key}")
        return False

    result = await cp.call(call.ChangeConfigurationPayload(key = connection_time_out_key[0]['key'], value = "6"))
    if(result == None or result.status != ConfigurationStatus.accepted):
        logging.info(f"Change configuration failed: {result}")
        return False

    result = await cp.call(call.GetConfigurationPayload([connection_time_out_key[0]['key'], "nonexistentkey"]))
    if(result == None):
        logging.info("GetConfiguration failed for specific and non existent key")
        return False

    if len(result.configuration_key) != 1 or result.unknown_key == None or len(result.unknown_key) != 1:
        logging.error(f"Unexpected result when getting specific and non-existent key: {result}")
        return False

    if result.configuration_key[0]['value'] != "6":
        logging.error(f'New value after change configuration is not the expected value: {result.configuration_key}')
        return False

    result = await cp.call(call.ChangeConfigurationPayload(key = "nonexistentkey", value = "3"))
    if(result == None or result.status != ConfigurationStatus.not_supported):
        logging.info(f"Change configuration with non-existent key failed: {result}")
        return False

    result_before = await cp.call(call.GetConfigurationPayload(None))
    if(result_before == None):
        logging.info("GetConfiguration for all values failed before reset hard")
        return False

    global boot_notification_payload
    boot_notification_payload = call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=0,
            status=RegistrationStatus.accepted
        )

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.hard))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test configuration persistence")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 120)
    except Exception as e:
        logging.error(f"Failed when waiting for BootNotification when testing change configuration: {e}")
        return False

    result_after = await cp.call(call.GetConfigurationPayload(None))
    if(result_after == None):
        logging.info("GetConfiguration for all values failed after reset hard")
        return False

    if result_after != result_before:
        logging.error(f"Configuration did not presist hard reset \n\n Before: {result_before} \n\n {result_after}")
        return False

    old_authorize = None
    for entry in result_after.configuration_key:
        if entry["key"] == "AuthorizationRequired":
            logging.info(f"Old authorize entry: {entry}")
            if entry["value"].lower() == "false" or entry["value"].lower() == "true":
                old_authorize = entry["value"].lower()
            else:
                logging.error(f'value of AuthorizationRequired does not convert to bool: {entry}')
                return False

    if old_authorize is None:
        logging.error(f"Unable to find AuthorizationRequired in get configuration response")
        return False

    new_authorize = "false" if old_authorize == "true" else "true"
    result = await cp.call(call.ChangeConfigurationPayload(key = "AuthorizationRequired", value = new_authorize))
    if(result == None or result.status != ConfigurationStatus.accepted):
        logging.info(f"Unable to change AuthorizationRequired to test persisntence")
        return False

    result = await cp.call(call.ResetPayload(type=ResetType.hard))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test configuration persistence")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.BootNotification, 120)
    except Exception as e:
        logging.error(f"Failed when waiting for BootNotification when testing change configuration after authorize required change and hard reset: {e}")
        return False

    result_update = await cp.call(call.GetConfigurationPayload(None))
    if(result == None):
        logging.info("GetConfiguration for all values failed after reset hard")
        return False

    update_success = False
    for i in range(len(result_update.configuration_key)):
        logging.info(f"Checking result {result_update.configuration_key[i]} against old {result_after.configuration_key[i]}")
        if result_update.configuration_key[i] != result_after.configuration_key[i]:
            if result_update.configuration_key[i]["key"] == "AuthorizationRequired" and result_update.configuration_key[i]["value"] == new_authorize:
                update_success = True
            else:
                logging.error(f'Configuration key mismatch after hard reset and change of AuthorizationRequired: {result_update.configuration_key[i]} and {result_after.configuration_key[i]}')
                return False

    if not update_success:
        logging.error(f'AuthorizationRequired did not persist after hard reset')
        return False

    result = await cp.call(call.ChangeConfigurationPayload(key = "AuthorizationRequired", value = old_authorize))
    if(result == None or result.status != ConfigurationStatus.accepted):
        logging.info(f"Unable to change AuthorizationRequired back after test")
        return False

    return True

async def test_change_availability(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.Authorize: asyncio.Event()
    }

    cp.additional_keys = [{
        'idTag': 'test_tag',
        'idTagInfo': dict(expiry_date = (datetime.utcnow() + timedelta(hours=1)).isoformat(), status='Accepted')
    }]

    conf_result = await ensure_configuration(cp, {ConfigurationKey.connection_time_out: "30"})
    if conf_result != 0:
        logging.error(f'Unable to configure connection timeout to test availability change: {conf_result}')
        return False

    if cp.connector0_status == ChargePointStatus.unavailable:
        result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 1, type = AvailabilityType.operative))
        if result == None or result.status != AvailabilityStatus.accepted:
            logging.error(f'Changing availablility to operative failed: {result}')
            return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if state != ChargePointStatus.available:
        logging.error(f"Failed to wait for status available to test availability change: {status}")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 1, type = AvailabilityType.inoperative))
    if result == None or result.status != AvailabilityStatus.accepted:
        logging.error(f'Changing availablility to inoperative failed: {result}')
        return False

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.unavailable:
        logging.error(f'Changeing availability to inoperative did not change status to unavailable')
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.rejected:
        logging.error("Remote start transaction was not rejected while in unavailable state")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 1, type = AvailabilityType.operative))
    if result == None or result.status != AvailabilityStatus.accepted:
        logging.error(f'Changing availablility to operative failed: {result}')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 16)
    if state != ChargePointStatus.available:
        logging.error(f"Failed to wait for status available after change to operative: {status}")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 1, type = AvailabilityType.operative))
    if result == None or result.status != AvailabilityStatus.accepted:
        logging.error(f'Changing availablility to operative from operative failed: {result}')
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error(f"Remote start transaction was not accepted while in available state to test availability change {result.status}")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.Authorize)
    except Exception as e:
        logging.error(f"Failed when waiting for Auth to test availability change in preparing state: {e}")
        return False

    connection_start = time.time()

    state = await wait_for_cp_status(cp, ChargePointStatus.preparing, 16)
    if state != ChargePointStatus.preparing:
        logging.error(f'CP did not transition to preparing after remote start to test change availability scheduled to after connection timeout')
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 0, type = AvailabilityType.inoperative))
    if result == None or result.status != AvailabilityStatus.scheduled:
        logging.error(f'Changing availablility to inoperative not scheduled in preparing state : {result.status}')
        return False

    non_preparing_states = [x for x in ChargePointStatus if x != ChargePointStatus.preparing]
    state = await wait_for_cp_status(cp, non_preparing_states, 35)
    if state != ChargePointStatus.finishing:
        if state == ChargePointStatus.preparing:
            logging.error("Failed to wait for finishing status when inoperative was scheduled from preparing with connection timeout")
        else:
            logging.error(f"Did not transition to finishing after connection timeout when scheduled for inoperative: {state}")
        return False

    non_finishing_states = [x for x in ChargePointStatus if x != ChargePointStatus.finishing]
    state = await wait_for_cp_status(cp, non_finishing_states, 16)
    if state != ChargePointStatus.unavailable:
        if state == ChargePointStatus.finishing:
            logging.error("Failed to wait for unavailable after state finishing due to connection timeout")
        else:
            logging.error(f"CP did not transition directly from finishing to unavailable after connection timeout: {state}")
        return False

    if (time.time() - connection_start) < 29:
        logging.error(f"CP did not wait for connection timeout before transitioning to unavailable due to scheduled inoperative")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 0, type = AvailabilityType.operative))
    if result == None or result.status != AvailabilityStatus.accepted:
        logging.error(f'Changing availablility to operative failed after scheduled inoperative: {result}')
        return False

    logging.info("Set to state B")
    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing])
    if state != ChargePointStatus.preparing:
        logging.error("Failed to wait for status preparing to test availability change")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 0, type = AvailabilityType.inoperative))
    if result == None or result.status != AvailabilityStatus.scheduled:
        logging.error(f'Changing availablility to inoperative not scheduled in state B: {result.status}')
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error(f"Remote start transaction was not accepted while scheduled for inoperative: {result.status}")
        return False

    state = await wait_for_cp_status(cp, non_preparing_states, 16)
    if state not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse]:
        logging.error(f"Expected CP to transition to charging but got: {state}")
        return False

    logging.info("Set to state A")
    state = await wait_for_cp_status(cp, [ChargePointStatus.available, ChargePointStatus.unavailable, ChargePointStatus.preparing])
    if state != ChargePointStatus.unavailable:
        logging.error(f"Expected CP to enter unavailable after scheduled inoperative and ended ChargeSession")
        return False

    result = await cp.call(call.ChangeAvailabilityPayload(connector_id = 0, type = AvailabilityType.operative))
    if result == None or result.status != AvailabilityStatus.accepted:
        logging.error(f'Changing availablility to operative failed after scheduled inoperative with car state B: {result}')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 16)
    if state != ChargePointStatus.available:
        logging.error("Failed to wait for status awailable after change availablility to operative")
        return False

    return True

async def test_faulted_state(cp, zap_in: ZapClientInput, zap_out: ZapClientOutput):

    if zap_in is None or zap_out is None:
        logging.error(f"Testing faulted state requires a connection to zapcli {zap_in} {zap_out}")
        return False

    zap_out.set_warning(MCUWarning.WARNING_EMETER_NO_RESPONSE)

    while cp.connector1_status != ChargePointStatus().faulted:
        logging.warning(F'Waiting for status faulted...{cp.connector1_status}')
        await asyncio.sleep(3)

    await asyncio.sleep(1) # Wait in case connector 1 was updated before connector 0 status

    if cp.connector0_status != ChargePointStatus.faulted:
        logging.error(F'Expected connector 0 to have faulted status')
        return False

    zap_out.set_warning(MCUWarning.WARNING_EMETER_NO_RESPONSE)

    return False

async def test_unlock_connector(cp):
    while cp.connector1_status != ChargePointStatus.preparing:
        logging.warning(f'Waiting for status preparing...{cp.connector1_status}')
        await asyncio.sleep(2)

    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, 'Is the connector sufficiently locked? y/n')

    if response != 'y':
        logging.error(f"User indicate connector did not connect: {response}")
        return False

    result = await cp.call(call.UnlockConnectorPayload(connector_id = 1))
    if result.status != UnlockStatus.unlocked:
        logging.error(f'Unable to unlock connector while in state preparing')
        return False

    response = await loop.run_in_executor(None, input, 'did the connector unlock? y/n')

    if response != 'y':
        logging.error("User indicate connector did not unlock correctly")
        return False

    result = await ensure_configuration(cp, {"AuthorizationRequired": "false"})
    if result != 0:
        logging.error("Unable to configure Authorization to test unlock")
        return False

    while cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev:
        logging.warning(f'Waiting for status charging...{cp.connector1_status}')
        await asyncio.sleep(2)

    result = await cp.call(call.UnlockConnectorPayload(connector_id = 1))
    if result.status != UnlockStatus.unlocked:
        logging.error(f'Unable to unlock connector while in state charging')
        return False

    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, 'did the connector unlock? y/n')

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.finishing and cp.connector1_status != ChargePointStatus.preparing:
        logging.error("Charging did not end after unlock connector")
        return False

    if response != 'y':
        logging.error("User indicate connector did not unlock correctly from state charging")
        return False

    return True

async def test_authorization_not_required(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event(),
        Action.StartTransaction: asyncio.Event(),
        Action.StopTransaction: asyncio.Event()
    }

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f"CP did not enter available to test RFID tag not required")
        return False

    preconfig_res = await ensure_configuration(cp, {'AuthorizationRequired': 'false',
                                                    'DefaultIdToken': 'test_tag_non',
                                                    ConfigurationKey.authorization_cache_enabled: 'false',
                                                    ConfigurationKey.local_auth_list_enabled: 'false',
                                                    ConfigurationKey.authorize_remote_tx_requests: 'false',
                                                    ConfigurationKey.clock_aligned_data_interval: '0',
                                                    ConfigurationKey.meter_value_sample_interval: '0'})

    if preconfig_res != 0:
        logging.error("Unable to configure CP to test authorization not required")
        return False

    cp.additional_keys = [{
        'idTag': 'test_tag_non',
        'idTagInfo': dict(expiry_date = (datetime.utcnow() + timedelta(hours=1)).isoformat(), status='Accepted')
    }]

    state = await wait_for_cp_status(cp, [ChargePointStatus.charging, ChargePointStatus.suspended_ev])
    if cp.connector1_status not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev]:
        logging.error("CP did not enter available to test presented RFID tag")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StartTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for start transaction: {e}")
        return False

    if cp.last_transaction_tag != 'test_tag_non':
        logging.error(f"CP did not use DefaultIdToken. Used {cp.last_transaction_tag}")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(cp.last_transaction_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of transaction without required RFID tag failed")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing], 16)
    if cp.connector1_status != ChargePointStatus.finishing:
        logging.error("CP did not enter finishing after remote stop to test authorization not required")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StopTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for stop transaction: {e}")
        return False

    if cp.last_transaction_stop_reason != Reason.remote:
        logging.error(f"CP indicate that transaction was stopped due to {cp.last_transaction_stop_reason}, when it should be {Reason.remote}")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing], 15)
    if cp.connector1_status != ChargePointStatus.finishing:
        logging.error(f"CP did not enter finishing after remote stop of transaction without requiering RFID tag")
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = key_list[0]['idTag'])) # Accepted key
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted in state finishing with authorization not required")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StartTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for start transaction after remote start in state finishing: {e}")
        return False

    if cp.last_transaction_tag != key_list[0]['idTag']:
        logging.error(f"CP did not use remote start tag while authorization was not required. Used {cp.last_transaction_tag}")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(cp.last_transaction_id))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote stop of transaction without required RFID tag failed after remote start in state finishing")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StopTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for stop transaction after remote stop of transaction started in state finishing with authorization not required: {e}")
        return False

    if cp.last_transaction_stop_reason != Reason.remote:
        logging.error(f"CP indicate that transaction was stopped due to {Reason}, when it should be {Reason.remote}")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.finishing], 15)
    if cp.connector1_status != ChargePointStatus.finishing:
        logging.error(f"CP did not enter finishing after remote stop of transaction without requiering RFID tag started in state finishing")
        return False

    invalid_key_entry = None
    for entry in key_list:
        if entry['idTagInfo']['status'] == AuthorizationStatus.invalid:
            invalid_key_entry = entry
            break

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = invalid_key_entry['idTag']))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted in state finishing with authorization not required and invalid idTag")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StartTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for start transaction: {e}")
        return False

    if cp.last_transaction_tag != invalid_key_entry['idTag']:
        logging.error("CP did not attempt to start transaction with the invalid tag sent in state finishing and authorization not required")
        return False

    try:
        await wait_for_cp_action_event(cp, Action.StopTransaction, 16)
    except Exception as e:
        logging.error(f"Failed when waiting for stop transaction due to deauth when authorization required is false: {e}")
        return False

    if cp.last_transaction_stop_reason != Reason.de_authorized:
        logging.error(f"CP did not stop transaction for {Reason.de_authorized} when starting transaction with invalid id from finishing state when authorization was not required")
        return False

    return True

async def test_core_profile(cp, zap_in, zap_out, include_manual_tests = True):
    logging.info(f'Setting up core profile test')
    preconfig_res = await ensure_configuration(cp,{ConfigurationKey.local_pre_authorize: "false",
                                                   ConfigurationKey.authorize_remote_tx_requests: "true",
                                                   ConfigurationKey.heartbeat_interval: "0",
                                                   "AuthorizationRequired": "true",
                                                   ConfigurationKey.connection_time_out: "7",
                                                   ConfigurationKey.clock_aligned_data_interval: "0",
                                                   ConfigurationKey.minimum_status_duration: "0"})

    if preconfig_res != 0:
        return -1

    def on_meter_value(self, call_unique_id, connector_id, meter_value, **kwargs):
        global awaiting_meter_connectors
        global last_meter_value_timestamps

        if connector_id in awaiting_meter_connectors:
            logging.info(f'Test got awaited meter value {call_unique_id}')
            global new_meter_value
            new_meter_values[connector_id] = meter_value
            awaiting_meter_connectors.remove(connector_id)
        else:
            logging.info(f'Test got meter value {call_unique_id}')

        recieved_date = dateutil.parser.isoparse(meter_value[0]['timestamp']).replace(tzinfo=None)
        logging.info(f'Created {datetime.utcnow().replace(tzinfo=None) - recieved_date} sec ago')
        if len(last_meter_value_timestamps) == 0 or last_meter_value_timestamps[-1] + timedelta(seconds=1) < recieved_date:
            last_meter_value_timestamps.append(recieved_date)

        return call_result.MeterValuesPayload()

    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_value, cp)

    if await test_change_availability(cp) != True:
        return False

    if include_manual_tests:
        if await test_got_presented_rfid(cp) != True:
            return False

    cp.additional_keys = list()
    if await test_remote_start(cp) != True:
       return False

    cp.additional_keys = list()
    if await test_meter_values(cp) != True:
        return False

    if await test_boot_notification_and_non_accepted_state(cp) != True:
        return False

    if await test_get_and_set_configuration(cp) != True:
        return False

    # if await test_faulted_state(cp, zap_in, zap_out) != True:
    #     return False

    if include_manual_tests:
        if await test_unlock_connector(cp) != True:
            return False

    if await test_authorization_not_required(cp) != True:
        return False

    logging.info("Core profile test complete successfully")
    return True
