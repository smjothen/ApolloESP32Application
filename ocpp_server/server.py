import asyncio
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
    ChargePointErrorCode
)
import random

charge_points = dict()
new_exipry_date = datetime.utcnow() + timedelta(days=1)

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
    response = await loop.run_in_executor(None, input, 'Input test id [A]ll/[C]ore/[R]eservation/[S]mart/[L]ocalAuthList/[T]riggerMessage/[E]ndurance Test: ')

    core_result = None
    reservation_result = None
    smart_charging_result = None
    local_auth_result = None
    remote_trigger_result = None

    if response == "C" or response == "A":
        core_result = await test_core_profile(cp)

    if response == "R" or response == "A":
        reservation_result = await test_reservation_profile(cp)

    if response == "S" or response == "A":
        smart_charging_result = await test_smart_charging_profile(cp)

    if response == "L" or response == "A":
        local_auth_result = await test_local_auth_list_profile(cp)

    if response == "T" or response == "A":
        remote_trigger_result = await test_remote_trigger_profile(cp)

    if response == "E":
        await endurance_tests(cp)

    logging.info(f'Core profile          : {core_result}')
    logging.info(f'Reservation profile   : {reservation_result}')
    logging.info(f'Smart charging profile: {smart_charging_result}')
    logging.info(f'Local auth list profile: {local_auth_result}')
    logging.info(f'Remote trigger profile: {remote_trigger_result}')

    await asyncio.sleep(10)

async def test_runner(cp):
    while True:
        try:
            await _test_runner(cp)
        except Exception as e:
            logging.error(f'Exception from test runner {e}')
            traceback.print_exc()
            print(e)

class ChargePoint(cp):
    cp.registration_status = RegistrationStatus.rejected
    cp.connector1_status = ChargePointStatus.unavailable

    @on('BootNotification')
    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        logging.info("replying to on boot msg")

        cp.registration_status = RegistrationStatus.accepted

        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=15,
            status=cp.registration_status
        )

    @on('Heartbeat')
    def on_heartbeat(self, **kwargs):
        logging.info("replying to heartbeat")

        time = datetime.utcnow().replace(tzinfo=timezone.utc).isoformat()
        time = datetime.utcnow().isoformat() + 'Z'

        return call_result.HeartbeatPayload(
            current_time=time,
        )

    @on('Authorize')
    def on_authorize_request(self, id_tag):
        logging.info(f'authorizing {id_tag}')
        return call_result.AuthorizePayload(
            dict(expiry_date = new_exipry_date.isoformat(), parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        )

    @on('StartTransaction')
    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info('Replying to start transaction')
        info=dict(expiryDate = new_exipry_date.isoformat(), parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=1231312
        )

    @on('MeterValues')
    def on_meter_value(self, connector_id, meter_value, **kwargs):
        logging.info(f'Meter value: {meter_value}')
        return call_result.MeterValuesPayload()

    @on('StopTransaction')
    def on_stop_transaction(self, **kwargs):
        logging.info("----------------------------------------")
        logging.info(f'Replying to stop transaction {kwargs}')
        logging.info("----------------------------------------")
        return call_result.StopTransactionPayload()

    @on('StatusNotification')
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
            logging.error(f'Connector {connector_id} status: {status} ({error_code})')

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

    del charge_points[charge-point_id]
    logging.info("End of on_connect")

async def main():
    logging.basicConfig(level=logging.WARNING, format='%(asctime)s %(levelname)-8s %(message)s')
    logging.getLogger('root').setLevel(logging.INFO)
    logging.getLogger('ocpp').setLevel(logging.WARNING)
    ip = '0.0.0.0'
    port = 9000

    logging.info(f'Creating websocket server at {ip}:{port}')
    server = await websockets.serve(
        on_connect,
        ip,
        port,
        subprotocols=['ocpp1.6'],
    )

    logging.info("WebSocket Server Started")
    await server.wait_closed()

if __name__ == '__main__':
    try:
        asyncio.run(main(), debug=True)
    except Exception:
        logging.error(f"Unhandled exception: {Exception}")
