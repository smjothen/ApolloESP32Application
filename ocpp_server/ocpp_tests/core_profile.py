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
    UnlockStatus
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import ensure_configuration
expecting_new_rfid = False
new_rfid = ''
authorize_response = call_result.AuthorizePayload(IdTagInfo(status=AuthorizationStatus.accepted))

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
        logging.error(f'Waiting for status preparing...{cp.connector1_status}')
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

    cp.route_map[Action.Authorize]["_on_action"] = types.MethodType(on_authorize, cp)

    # if include_manual_tests:
    #     if await test_got_presented_rfid(cp) != True:
    #         return False

    # if await test_remote_start(cp) != True:
    #     return False

    # if await test_boot_notification_and_non_accepted_state(cp) != True:
    #     return False

    # if await test_get_and_set_configuration(cp) != True:
    #     return False

    if await test_change_availability(cp) != True:
        return False

    if include_manual_tests:
        if await test_unlock_connector(cp) != True:
            return False

    logging.info("Core profile test complete successfully")
    return True
