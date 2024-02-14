import argparse
import itertools
import asyncio
from functools import wraps
import serial_asyncio
import traceback
import websockets
import time
import datetime
from ocpp_tests.core_profile import test_core_profile
from ocpp_tests.reservation_profile import test_reservation_profile
from ocpp_tests.smart_charging_profile import test_smart_charging_profile
from ocpp_tests.local_auth_list_profile import test_local_auth_list_profile
from ocpp_tests.remote_trigger_profile import test_remote_trigger_profile
from ocpp_tests.endurance_tests import endurance_tests
from ocpp_tests.websocket_test import test_websocket
from ocpp_tests.test_utils import(
    ZapClientInput,
    ZapClientOutput
)
from ocpp_tests.keys import key_list

import logging

from datetime import(
    datetime,
    timezone,
    timedelta
)

from ocpp.routing import on
from ocpp.v16 import ChargePoint as cp
from ocpp.v16 import call_result, call
from ocpp.v16.enums import (
    Action,
    ResetType,
    RegistrationStatus,
    ChargePointStatus,
    ChargePointErrorCode,
    AuthorizationStatus,
    Reason
)
import random

charge_points = dict()
new_exipry_date = datetime.utcnow() + timedelta(days=1)

zap_in = None
zap_out = None

async def init_zap_client(path):
    loop = asyncio.get_event_loop()

    print(f'Setting up zapclient on {path}')
    transport, protocol = await serial_asyncio.create_serial_connection(loop, ZapClientInput, path, baudrate=115200)

    global zap_in
    global zap_out

    zap_out = ZapClientOutput(transport)
    zap_in = protocol

    zap_out.enable()
    zap_out.attempt_ready()

async def _test_runner(cp):

    i = 0
    while cp.registration_status != RegistrationStatus.accepted:
        i+=1
        if i > 10:
            logging.info("Attempting reset due to no bootNotification")
            await cp.call(call.ResetPayload(type = ResetType.hard))
            i = 0

        logging.warning(f"Awaiting accepted boot...({cp.registration_status})")
        await asyncio.sleep(2)


    logging.info("Boot accepted")

    loop = asyncio.get_event_loop()
    response = await loop.run_in_executor(None, input, 'Input test id [A]ll/[C]ore/[R]eservation/[S]mart/[L]ocalAuthList/[T]riggerMessage/[E]ndurance Test/[W]ebsocket: ')

    core_result = None
    reservation_result = None
    smart_charging_result = None
    local_auth_result = None
    remote_trigger_result = None
    websocket_result = None

    response = response.upper();
    if response == "C" or response == "A":
        core_result = await test_core_profile(cp, zap_in, zap_out)

    if response == "R" or response == "A":
        reservation_result = await test_reservation_profile(cp)

    if response == "S" or response == "A":
        smart_charging_result = await test_smart_charging_profile(cp)

    if response == "L" or response == "A":
        local_auth_result = await test_local_auth_list_profile(cp)

    if response == "T" or response == "A":
        remote_trigger_result = await test_remote_trigger_profile(cp)

    if response == "W" or response == "A":
        websocket_result = await test_websocket(cp)

    if response == "E":
        await endurance_tests(cp)

    logging.info(f'Core profile             : {core_result}')
    logging.info(f'Reservation profile      : {reservation_result}')
    logging.info(f'Smart charging profile   : {smart_charging_result}')
    logging.info(f'Local auth list profile  : {local_auth_result}')
    logging.info(f'Remote trigger profile   : {remote_trigger_result}')
    logging.info(f'websocket_result         : {websocket_result}')

    await asyncio.sleep(10)

    return True

async def test_runner(cp):
    while True:
        try:
            result = await _test_runner(cp)
            if not result:
                return

        except Exception as e:
            logging.error(f'Exception from test runner {e}')
            traceback.print_exc()
            print(e)


class ChargePoint(cp):

    def __init__(self, id, connection, response_timeout=30):
        super().__init__(id, connection, response_timeout)

        #boot related
        self.registration_status = RegistrationStatus.rejected
        self.connector1_status = ChargePointStatus.unavailable
        self.boot_status = RegistrationStatus.accepted
        self.boot_interval = 15
        self.boot_timestamp = None # TODO: Consider implementing as time difference and/or set time

        #transaction related
        self.transaction_is_active = False
        self.last_transaction_tag = None
        self.last_transaction_id = None
        self.last_transaction_stop_reason = Reason.other
        self.last_transaction_tag_used_for_stopping = None

        #Authorization related
        self.last_auth_tag = None
        self.last_auth_info = None
        self.additional_keys = list()

        self.action_events = dict()


    @on(Action.BootNotification)
    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("replying to on boot msg")

        self.registration_status = self.boot_status

        if Action.BootNotification in self.action_events:
            self.action_events[Action.BootNotification].set()

        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat() if self.boot_timestamp is None else self.boot_timestamp,
            interval=self.boot_interval,
            status=self.registration_status
        )

    @on(Action.Heartbeat)
    def on_heartbeat(self, **kwargs):
        logging.info("replying to heartbeat")

        #time = datetime.utcnow().replace(tzinfo=timezone.utc).isoformat()
        time = datetime.utcnow().isoformat() + 'Z'

        if Action.Heartbeat in self.action_events:
            self.action_events[Action.Heartbeat].set()

        return call_result.HeartbeatPayload(
            current_time=time
        )

    def tag_info_from_tag(self, id_tag):
        for entry in itertools.chain(self.additional_keys, key_list):
            if entry['idTag'] == id_tag or entry['idTag'] == '*':
                return entry['idTagInfo']

        return dict(status=AuthorizationStatus.invalid)

    @on(Action.Authorize)
    def on_authorize_request(self, id_tag):
        logging.info(f'authorizing {id_tag}')

        self.last_auth_info = self.tag_info_from_tag(id_tag)
        self.last_auth_tag = id_tag

        if Action.Authorize in self.action_events:
            self.action_events[Action.Authorize].set()

        return call_result.AuthorizePayload(self.last_auth_info)

    @on(Action.StartTransaction)
    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        id_tag_info_stored = self.tag_info_from_tag(id_tag)
        logging.info(f'Replying to start transaction with id: {id_tag} {id_tag_info_stored}')

        self.last_transaction_id = 1 if self.last_transaction_id == None else self.last_transaction_id +1
        self.last_transaction_tag = id_tag
        self.transaction_is_active = True

        if Action.StartTransaction in self.action_events:
            self.action_events[Action.StartTransaction].set()

        return call_result.StartTransactionPayload(
            id_tag_info = self.tag_info_from_tag(id_tag),
            transaction_id = self.last_transaction_id
        )

    @on(Action.MeterValues)
    def on_meter_value(self, call_unique_id, connector_id, meter_value, **kwargs):
        logging.info(f'Meter value: {meter_value}')
        return call_result.MeterValuesPayload()

    @on(Action.StopTransaction)
    def on_stop_transaction(self, meter_stop, timestamp, transaction_id, **kwargs):
        self.transaction_is_active = False
        if self.last_transaction_id == None:
            self.last_transaction_id = transaction_id

        logging.info(f"Kwargs: {kwargs}")
        if 'reason' in kwargs:
            self.last_transaction_stop_reason = kwargs['reason']

        if 'id_tag' in kwargs:
            self.last_transaction_tag_used_for_stopping = kwargs['id_tag']

        logging.info("----------------------------------------")
        logging.info(f'Replying to stop transaction {transaction_id}')
        logging.info("----------------------------------------")

        if Action.StopTransaction in self.action_events:
            self.action_events[Action.StopTransaction].set()

        return call_result.StopTransactionPayload()

    @on(Action.StatusNotification)
    def on_status_notification(self, connector_id, error_code, status, **kwargs):
        if connector_id == 0:
            self.connector0_status = status
        elif connector_id == 1:
            self.connector1_status = status
        else:
            logging.error(f'Unexpected connector {connector_id}')

        if error_code == ChargePointErrorCode.no_error:
            logging.info(f'Connector {connector_id} status: {status} ({error_code})')
        else:
            logging.error(f'Connector {connector_id} status: {status} ({error_code}) {kwargs}')

        if Action.StatusNotification in self.action_events:
            self.action_events[Action.StatusNotification].set()

        return call_result.StatusNotificationPayload()

    @on(Action.DiagnosticsStatusNotification)
    def on_diagnostics_status_notification(self, status):
        logging.info(f"Got diagnostics status: {status}")
        return call_result.DiagnosticsStatusNotificationPayload()

    @on(Action.FirmwareStatusNotification)
    def on_firmware_status_notification(self, status):
        logging.info(f"Got firmware status: {status}")

        return FirmwareStatusNotificationPayload()

async def on_connect(websocket, path):
    """ For every new charge point that connects, create a ChargePoint instance
    and start listening for messages.

    """

    global charge_points

    charge_point_id = path.strip('/')
    if charge_point_id in charge_points:
        logging.warning(f"Chargepoint with id {charge_point_id} reconnected")
        charge_points[charge_point_id]._connection = websocket

        try:
            await charge_points[charge_point_id].start()
        except Exception as e:
            logging.error(f"Chargepoint raised exception: {e}")
        return
    else:
        charge_points[charge_point_id] = ChargePoint(charge_point_id, websocket)

        logging.info("Starting tests")

        test_task = asyncio.create_task(test_runner(charge_points[charge_point_id]))

        try:
            await charge_points[charge_point_id].start()
        except Exception as e:
            logging.error(f"Chargepoint raised exception: {e}")

        await test_task

    del charge_points[charge_point_id]

async def main():
    logging.basicConfig(level=logging.WARNING, format='%(asctime)s %(levelname)-8s %(message)s')
    logging.getLogger('root').setLevel(logging.INFO)
    logging.getLogger('ocpp').setLevel(logging.WARNING)
    ip = '0.0.0.0'
    port = 9000

    parser = argparse.ArgumentParser()
    parser.add_argument("-z", "--zap_cli_path", help="Path to serial device for communication with MCU. Example -z /dev/ttyUSB1")
    args = parser.parse_args()

    if args.zap_cli_path:
        await init_zap_client(args.zap_cli_path)

    logging.info(f'Creating websocket server at {ip}:{port}')
    server = await websockets.serve(
        on_connect,
        ip,
        port,
        subprotocols=['ocpp1.6'],
    )

    logging.info("Hello")
    await server.wait_closed()

if __name__ == '__main__':

    try:
        asyncio.run(main(), debug=True)
    except Exception:
        logging.error(f"Unhandled exception: {Exception}")
