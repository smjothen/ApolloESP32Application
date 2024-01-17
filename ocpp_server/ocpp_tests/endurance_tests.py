#!/usr/bin/env python3
import math
import random
import types
import asyncio
import logging
from datetime import datetime, timedelta
import dateutil.parser
import time

from ocpp.routing import on
from ocpp.v16 import call_result, call
from ocpp.v16.enums import (
    AvailabilityStatus,
    AvailabilityType,
    ConfigurationKey,
    ConfigurationStatus,
    ChargingProfilePurposeType,
    ChargingProfileKindType,
    ChargingRateUnitType,
    ClearChargingProfileStatus,
    ChargingProfileStatus,
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
from ocpp.v16.datatypes import(
    ChargingProfile,
    IdTagInfo,
    ChargingSchedule,
    ChargingSchedulePeriod
)
from ocpp.v16.datatypes import IdTagInfo
from ocpp_tests.test_utils import ensure_configuration
from ocpp_tests.smart_charging_profile import create_charging_profile

expecting_boot_notification = False
unexpected_boot_count = 0
expected_boot_count = 0
awaiting_value = 0

schedule_start = 0
schedule_limits = None
schedule_switch_rate = 0
awaiting_meter_value = False
expected_value_count = 0
almost_value_count = 0
unexpected_value_count = 0
tx_id = 180

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


async def smart_charging_extended_with_rapid_meter_values(cp):
    logging.info("Starting smart charging endurance test")

    preconfig_res = await ensure_configuration(cp, {ConfigurationKey.local_pre_authorize: "false",
                                                    ConfigurationKey.authorize_remote_tx_requests: "false",
                                                    ConfigurationKey.heartbeat_interval: "0",
                                                    ConfigurationKey.clock_aligned_data_interval: "0",
                                                    ConfigurationKey.meter_value_sample_interval: "2",
                                                    ConfigurationKey.meter_values_sampled_data: Measurand.current_offered,
                                                    "AuthorizationRequired": "false"})

    if preconfig_res != 0:
        logging.error("Unable to prepare for smart charging endurance test")
        return False

    result = await cp.call(call.ClearChargingProfilePayload())
    if result is None:
        logging.error('Unable to clear charging profiles to prepare for endurance test')
        return False

    result = await cp.call(call.GetConfigurationPayload([ConfigurationKey.charging_schedule_max_periods]))
    if result == None:
        logging.error("Unable to get configuration to determine schedule length")
        return False


    if result.configuration_key == None or len(result.configuration_key) != 1 or result.configuration_key[0]["key"] != ConfigurationKey.charging_schedule_max_periods:
        logging.error(f'Unexpected reply when getting charging schedule max periods: {result}')
        return False

    max_periods = int(result.configuration_key[0]["value"])
    if max_periods < 6 or max_periods > 3600:
        logging.error(f"Unexpected max periods: {max_periods}")
        return False

    global schedule_limits
    global schedule_switch_rate

    schedule_limits = [random.randrange(6, 33, 1) for i in range(max_periods)]
    schedule_limits[5] = 0

    # Default maximum number of session resets is 20 per hour. We do one reset per schedule
    schedule_switch_rate = math.ceil((3600 / 20) / max_periods)
    logging.info(f'Will attempt to switch period every {schedule_switch_rate} sec')

    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info(f"Testing got start transaction with id {id_tag}")
        info=dict(parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        global tx_id
        global tx_start
        tx_start = datetime.utcnow()
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=tx_id
        )

    def on_meter_values(self, connector_id, meter_value, transaction_id, **kwargs):
        global awaiting_meter_value
        global expected_value_count
        global unexpected_value_count
        global almost_value_count

        if awaiting_meter_value:
            recieved_value = -1
            awaiting_value_offset = 0
            try:
                if len(meter_value) == 1 and meter_value[0]['sampled_value'][0]['measurand'] == Measurand.current_offered:
                    recieved_value = int(float(meter_value[0]['sampled_value'][0]['value']))
                    recieved_date = dateutil.parser.isoparse(meter_value[0]['timestamp']).replace(tzinfo=None)
                    awaiting_value_offset = math.floor((recieved_date - schedule_start).total_seconds())
                else:
                    logging.warning(f'Got unexpected meter value structure {meter_value}')

            except Exception as e:
                logging.error(f'Unable to interpret meter value: {e}')

            awaiting_value_index = awaiting_value_offset // schedule_switch_rate
            if awaiting_value_index >= len(schedule_limits):
                awaiting_value_index = len(schedule_limits)-1

            logging.info(f'offset: {awaiting_value_offset} sec. Index {awaiting_value_offset // schedule_switch_rate}')

            if recieved_value == schedule_limits[awaiting_value_index]:
                logging.info(f'Got expected value {recieved_value}')
                expected_value_count+=1
            elif awaiting_value_offset > 1 and (awaiting_value_offset -1) // schedule_switch_rate != awaiting_value_index and recieved_value == schedule_limits[awaiting_value_index-1]:
               logging.info(f'Got value within margin {recieved_value}')
               almost_value_count+=1
            else:
                logging.warning(f'Got unexpected value. Expected {awaiting_value} got {recieved_value}')
                unexpected_value_count+=1

        return call_result.MeterValuesPayload()

    cp.route_map[Action.StartTransaction]["_on_action"] = types.MethodType(on_start_transaction, cp)
    cp.route_map[Action.MeterValues]["_on_action"] = types.MethodType(on_meter_values, cp)

    global schedule_start
    limits = schedule_limits
    time_to_next_change = schedule_switch_rate
    tx_profile = create_charging_profile(transaction_id = tx_id,
                                         schedule = ChargingSchedule(
                                             charging_rate_unit = ChargingRateUnitType.amps,
                                             charging_schedule_period = [
                                                 ChargingSchedulePeriod(start_period = i * time_to_next_change, limit = x, number_phases = 3 if i % 2 else 1) for i, x in enumerate(limits)
                                         ]))

    global awaiting_value
    global awaiting_meter_value
    global expected_value_count
    global unexpected_value_count

    itteration = 0
    while True:
        while(cp.connector1_status != ChargePointStatus.charging and cp.connector1_status != ChargePointStatus.suspended_ev):
            logging.warning(f"Waiting for status charging...({cp.connector1_status})")
            await asyncio.sleep(3)

        schedule_start = datetime.utcnow()
        tx_profile.charging_schedule.start_schedule = schedule_start.isoformat() + 'Z'

        result = await cp.call(call.SetChargingProfilePayload(connector_id = 1, cs_charging_profiles = tx_profile))
        if result is None or result.status != ChargingProfileStatus.accepted:
            logging.info('Unable to set tx profile during endurance test')
            return False

        awaiting_value = 0
        awaiting_meter_value = True

        for i, limit in enumerate(limits):
            timeline = [">{limit:2d}".format(limit = limit) if i == j else "{limit:3d}".format(limit = limit) for j, limit in enumerate(limits)]
            logging.info(" ".join(timeline))
            awaiting_value = limit
            await asyncio.sleep(time_to_next_change)

        awaiting_meter_value = False

        itteration+=1
        logging.info(f"Smart charging itteration {itteration} Expected count: {expected_value_count}, Almost count: {almost_value_count}, Unexpected count {unexpected_value_count}")

async def endurance_tests(cp):
    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, 'Input sub id [B]oot repeat/[S]smart charging extended')

    if response == "B":
        await boot_repeat(cp)

    if response == "S":
        await smart_charging_extended_with_rapid_meter_values(cp)


    logging.error("Endurance test exited")
