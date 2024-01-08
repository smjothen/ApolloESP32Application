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
expecting_new_rfid = False
new_rfid = ''
authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted))

awaiting_meter_connectors = []
new_meter_values = dict()

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
    global authorize_response
    authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
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

    await asyncio.sleep(3)
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
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

    await asyncio.sleep(3)
    if cp.connector1_status != ChargePointStatus.available:
        logging.error(f'CP did not remain in available after accept and expire Authorize.req: {cp.connector1_status}')
        return False

    conf_result = await ensure_configuration(cp, {ConfigurationKey.connection_time_out: "20"})
    if conf_result != 0:
        logging.error(f'Unable to configure longer connection timeout: {conf_result}')
        return False

    authorize_response= call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted, expiry_date=(datetime.utcnow() + timedelta(days=1)).isoformat()))
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

    i = 0
    while cp.connector1_status != ChargePointStatus.preparing:
        i+=1
        if i > 8:
            logging.error('Did not get transition to preparing')
            return False
        logging.warning('awaiting transition due to remote start accept')
        await asyncio.sleep(0.3)

    i = 0
    while cp.connector1_status != ChargePointStatus.available:
        i+=1
        if i > 8:
            break
        logging.warning(f'Awaiting longer connection timeout')
        await asyncio.sleep(2)

    if i < 8:
        logging.error(f'longer connection timout was not long')
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

        res = await cp.call(call.ChangeConfigurationPayload(interval_type, "0"))
        if(result == None or change_res.status != ConfigurationStatus.accepted):
            logging.error(f"Unable to turn of clock aligned interval")
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
    boot_notification_payload = call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=15,
            status=RegistrationStatus.accepted
        )
    global expecting_boot_notification
    expecting_boot_notification = True

    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("Test got boot msg")
        global expecting_boot_notification
        expecting_boot_notification = False

        return boot_notification_payload

    cp.route_map[Action.BootNotification]["_on_action"] = types.MethodType(on_boot_notitication, cp)

    cp.connector1_status = ChargePointStatus.unavailable
    result = await cp.call(call.ResetPayload(type=ResetType.soft))
    if result.status != ResetStatus.accepted:
        logging.error("Unable to reset charger to test boot notification")
        return False

    i = 0
    while expecting_boot_notification:
        i += 1
        if i > 10:
            logging.error("Did not get boot within expected delay")
            return False

        logging.warning("Awaiting new boot notification...")
        await asyncio.sleep(2)

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

async def test_core_profile(cp, include_manual_tests = True):
    logging.info('Setting up core profile test')
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

    def on_meter_value(self, connector_id, meter_value, **kwargs):
        global awaiting_meter_connectors

        if connector_id in awaiting_meter_connectors:
            logging.info(f'Test got awaited meter value')
            global new_meter_value
            new_meter_values[connector_id] = meter_value
            awaiting_meter_connectors.remove(connector_id)
        else:
            logging.info(f'Test got meter value')

        return call_result.MeterValuesPayload()

    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize, cp)
    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_value, cp)

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

    if include_manual_tests:
        if await test_unlock_connector(cp) != True:
            return False

    logging.info("Core profile test complete successfully")
    return True
