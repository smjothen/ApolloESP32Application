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
    Phase
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import (
    ensure_configuration,
    ZapClientOutput,
    ZapClientInput,
    OperationState,
    MCUWarning,
    wait_for_cp_status
)
expecting_boot_notification = False
boot_notification_payload = boot_notification_payload = call_result.BootNotificationPayload(
    current_time=datetime.utcnow().isoformat(),
    interval=0,
    status=RegistrationStatus.accepted
)

tx_id = 500
expecting_new_rfid = False
new_rfid = ''
authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted))

awaiting_meter_connectors = []
new_meter_values = dict()
last_meter_value_timestamps = deque(maxlen = 10)

async def test_got_presented_rfid(cp):
    cp.action_events = {
        Action.StatusNotification: asyncio.Event()
    }

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f"CP did not enter available to test presented RFID tag")
        return False

    authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))

    global expecting_new_rfid
    expecting_new_rfid = True

    while(expecting_new_rfid):
        logging.warning(f"Waiting for RFID tag...")
        await asyncio.sleep(3)

    global new_rfid
    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, f'does "{new_rfid}" match presented id? y/n ')

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
        Action.StatusNotification: asyncio.Event()
    }

    state = await wait_for_cp_status(cp, [ChargePointStatus.available])
    if state != ChargePointStatus.available:
        logging.error("CP did not enter available to start remote_start_test")
        return False

    global expecting_new_rfid
    global authorize_response
    authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
    expecting_new_rfid = True
    timeout_start = time.time()
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing], 10)
    if state != ChargePointStatus.preparing:
        logging.error(f'CP did not enter preparing after accepted Authorize.req: {cp.connector1_status}')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 16)
    if state != ChargePointStatus.available:
        logging.error("CP did not enter avaialble after connection timeout")
        return False

    if (time.time() - timeout_start) < 7:
        logging.error(f"CP did not wait for Connection timeout. Waited for less than {int(time.time() - timeout_start)}")
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.concurrent_tx))
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 10)
    if state != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after concurrent tx Authorize.req: {cp.connector1_status}')
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.blocked))
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 10)
    if state != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after after blocked Authorize.req: {cp.connector1_status}')
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.invalid))
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 10)
    if state != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after invalid Authorize.req: {cp.connector1_status}')
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.expired))
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 10)
    if state != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after expired Authorize.req: {cp.connector1_status}')
        return False

    # accepted but actually expired
    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() - timedelta(days=1)).isoformat()))
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 10)
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after accept and expire Authorize.req: {cp.connector1_status}')
        return False

    conf_result = await ensure_configuration(cp, {ConfigurationKey.connection_time_out: "20"})
    if conf_result != 0:
        logging.error(f'Unable to configure longer connection timeout: {conf_result}')
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
    expecting_new_rfid = True
    timeout_start = time.time()
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

    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing], 10)
    if state != ChargePointStatus.preparing:
        logging.error('Did not get transition to preparing')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.available], 25)
    if state != ChargePointStatus.available:
        logging.error("CP did not go back to available after longer connection timeout")
        return False

    if (time.time() - timeout_start) < 20:
        logging.error(f'longer connection timout was not long')
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.preparing])
    while cp.connector1_status != ChargePointStatus.preparing:
        logging.error("Car did not connect to test remote start during preparing")
        return False

    authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
    expecting_new_rfid = True
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in preparing state")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse], 16)
    if state not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse]:
        logging.error("CP did not enter state charging after all preconditions where met")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(tx_id))
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

    authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
    expecting_new_rfid = True
    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in finishing state")
        return False

    state = await wait_for_cp_status(cp, [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse], 16)
    if state not in [ChargePointStatus.charging, ChargePointStatus.suspended_ev, ChargePointStatus.suspended_evse]:
        logging.error("CP did not enter state charging after all preconditions where met in finishing state")
        return False

    result = await cp.call(call.RemoteStopTransactionPayload(tx_id))
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
        while(cp.connector1_status not in wanted_state):
            logging.warning(f"Waiting for status {wanted_state}...({cp.connector1_status})")
            await asyncio.sleep(3)

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

    while(cp.connector1_status != ChargePointStatus.available and cp.connector1_status != ChargePointStatus.preparing):
        logging.warning(f"Waiting for status available...({cp.connector1_status})")
        await asyncio.sleep(3)

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
    global boot_notification_payload
    boot_notification_payload = call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=15,
            status=RegistrationStatus.accepted
        )
    global expecting_boot_notification
    expecting_boot_notification = True

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test boot notification")
        return False

    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 15:
            logging.error("Did not get boot within expected delay")
            return False

        logging.warning("Awaiting new boot notification...")
        await asyncio.sleep(3)

    # Attempt boot trigger as it should not be allowed while accepted
    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.boot_notification))
    if result.status != TriggerMessageStatus.rejected:
        logging.error(f"Trigger was not rejected as suggested by errata v4.0. Got: {result}")
        return False

    i = 0
    while cp.connector1_status == ChargePointStatus.unavailable:
        i += 1
        if i > 5:
            logging.error("Boot accepted but CP status is unavailable")
            return False

        logging.warning("waiting for connector to exit unavailable...")
        await asyncio.sleep(3)

    # TODO: Find a way to test if heartbeat interval was set correctly with the boot_notification_payload interval. Any message may substiture heartbeat.
    # Just waiting for heartbeat.req may not be enough.

    boot_notification_payload.interval = 30
    boot_notification_payload.status = RegistrationStatus.pending

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test second boot notification")
        return False

    expecting_boot_notification = True
    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 10:
            logging.error("Did not get second boot within expected delay")
            return False
        logging.warning("Awaiting second boot notification test...")
        await asyncio.sleep(2)

    config_result = await ensure_configuration(cp, {ConfigurationKey.heartbeat_interval: "30"})
    if config_result != 0:
        logging.error("Unable to configure charger in pending state")
        return False

    expecting_boot_notification = True
    logging.warning("Waiting for interval timeout")
    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 32:
            logging.error("Did not get interval timeout boot notification")
            return False
        await asyncio.sleep(1)

    if i < 25:
        logging.error("Got boot notification before it was expected")
        return False

    boot_notification_payload.interval = 0
    boot_notification_payload.status = RegistrationStatus.rejected
    expecting_boot_notification = True

    result = await cp.call(call.TriggerMessagePayload(requested_message = MessageTrigger.boot_notification))
    if result.status != TriggerMessageStatus.accepted:
        logging.error(f"Trigger was not accepted as during pending. Got: {result}")
        return False

    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 4:
            logging.error("Boot trigger accepted but no notification sent")
            return False
        await asyncio.sleep(2)

    logging.warning("Waiting for message timeout and default retry interval (Expect significat delay (30 sec then 90 sec))")
    config_result = await ensure_configuration(cp, {ConfigurationKey.heartbeat_interval: 30})
    if config_result == 0:
        logging.error("Was able to incorrectly configure charger while rejected")
        return False

    boot_notification_payload.status = RegistrationStatus.accepted
    expecting_boot_notification = True
    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 60:
            logging.error("Did not get bootnotification given CP set interval.")
            return False
        await asyncio.sleep(5)

    return True

async def test_get_and_set_configuration(cp):
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

    global expecting_boot_notification

    expecting_boot_notification = True
    cp.connector1_status = ChargePointStatus.unavailable

    result = await cp.call(call.ResetPayload(type=ResetType.hard))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test configuration persistence")
        return False

    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 15:
            logging.error("Did not get boot within expected delay")
            return False

        logging.warning("Awaiting new boot notification...")
        await asyncio.sleep(3)

    result_after = await cp.call(call.GetConfigurationPayload(None))
    if(result_after == None):
        logging.info("GetConfiguration for all values failed after reset hard")
        return False

    if result_after != result_before:
        logging.error(f"Configuration did not presist hard reset \n\n Before: {result_before} \n\n {result_after}")
        return False

    old_authorize = "Non"
    for entry in result_after.configuration_key:
        if entry["key"] == "AuthorizationRequired":
            if entry["value"].lower() == "false" or entry["value"].lower() == "true":
                old_authorize = entry["value"].lower()
            else:
                logging.error(f'value of AuthorizationRequired does not convert to bool: {entry}')
                return False

    new_authorize = "false" if old_authorize == "true" else "false"
    result = await cp.call(call.ChangeConfigurationPayload(key = "AuthorizationRequired", value = new_authorize))
    if(result == None or result.status != ConfigurationStatus.accepted):
        logging.info(f"Unable to change AuthorizationRequired to test persisntence")
        return False

    expecting_boot_notification = True
    cp.connector1_status = ChargePointStatus.unavailable

    result = await cp.call(call.ResetPayload(type=ResetType.hard))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test configuration persistence")
        return False

    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 15:
            logging.error("Did not get boot within expected delay")
            return False

        logging.warning("Awaiting new boot notification...")
        await asyncio.sleep(2)

    result_update = await cp.call(call.GetConfigurationPayload(None))
    if(result == None):
        logging.info("GetConfiguration for all values failed after reset hard")
        return False

    update_success = False
    for i in range(len(result_update.configuration_key)):
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f'Changeing availability to operative did not change status to available')
        return False

    result = await cp.call(call.RemoteStartTransactionPayload(id_tag = "test_tag"))
    if result.status != RemoteStartStopStatus.accepted:
        logging.error("Remote start transaction was not accepted while in available state")
        return False

    while cp.connector1_status != ChargePointStatus.preparing:
        logging.warning(f'Waiting for remote start to take effect. Expecting state {ChargePointStatus.preparing} currently {cp.connector1_status}')
        await asyncio.sleep(2)

    while cp.connector1_status != ChargePointStatus.available:
        logging.warning(f'Waiting for auth timeout. Expecting state {ChargePointStatus.available} currently {cp.connector1_status}')
        await asyncio.sleep(2)

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

async def test_core_profile(cp, zap_in, zap_out, include_manual_tests = True):
    logging.info(f'Setting up core profile test')
    preconfig_res = await ensure_configuration(cp,{ConfigurationKey.local_pre_authorize: "false",
                                                   ConfigurationKey.authorize_remote_tx_requests: "true",
                                                   ConfigurationKey.heartbeat_interval: "0",
                                                   "AuthorizationRequired": "true",
                                                   ConfigurationKey.connection_time_out: "7",
                                                   ConfigurationKey.clock_aligned_data_interval: "0",
                                                   ConfigurationKey.minimum_status_duration: "0"});

    if preconfig_res != 0:
        return -1

    def on_authorize(self, id_tag):
        logging.info(f"Testing got rfid tag {id_tag}")
        global authorize_response
        global expecting_new_rfid
        global new_rfid

        if expecting_new_rfid:
            new_rfid = id_tag
            expecting_new_rfid = False

        return authorize_response

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

    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("Test got boot msg")
        global expecting_boot_notification
        expecting_boot_notification = False

        boot_notification_payload.current_time=datetime.utcnow().isoformat()

        return boot_notification_payload

    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info(f"Testing got start transaction with id {id_tag}")
        info=dict(parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        global tx_id
        tx_id +=1
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=tx_id
        )

    cp.route_map[Action.BootNotification]["_on_action"] = types.MethodType(on_boot_notitication, cp)
    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize, cp)
    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_value, cp)
    cp.route_map[Action.StartTransaction]["_on_action"] = types.MethodType(on_start_transaction, cp)

    if include_manual_tests:
        if await test_got_presented_rfid(cp) != True:
            return False

    if await test_remote_start(cp) != True:
       return False

    if await test_meter_values(cp) != True:
        return False

    if await test_boot_notification_and_non_accepted_state(cp) != True:
        return False

    if await test_get_and_set_configuration(cp) != True:
        return False

    if await test_change_availability(cp) != True:
        return False

    # if await test_faulted_state(cp, zap_in, zap_out) != True:
    #     return False

    if include_manual_tests:
        if await test_unlock_connector(cp) != True:
            return False

    logging.info("Core profile test complete successfully")
    return True
