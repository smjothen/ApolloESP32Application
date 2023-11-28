import asyncio
import websockets
import time
import datetime
from ocpp_tests.core_profile import test_core_profile
from ocpp_tests.smart_charging_profile import test_smart_charging_profile
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
    ResetType,
    RegistrationStatus,
    ChargePointStatus,
    ChargePointErrorCode
)
import random

new_exipry_date = datetime.utcnow() + timedelta(days=1)

async def test_runner(cp):

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

    core_result = await test_core_profile(cp)
    smart_charging_result = await test_smart_charging_profile(cp)

    logging.info(f'Core profile          : {core_result}')
    logging.info(f'Smart charging profile: {smart_charging_result}')

    await asyncio.sleep(10)

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

    @on('MeterValues')
    def on_meter_value_request(self, **kwargs):
        logging.info('meter value')

    @on('StartTransaction')
    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        logging.info('Replying to start transaction')
        info=dict(expiryDate = new_exipry_date.isoformat(), parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=1231312
        )

    @on('MeterValues')
    def on_meter_value(self, connector_id, meter_value, transaction_id, **kwargs):
        logging.info(f'Meter value: {kwargs}')
        return call_result.MeterValuesPayload()

    @on('StopTransaction')
    def on_stop_transaction(self, **kwargs):
        logging.info("----------------------------------------")
        logging.info(f'Replying to stop transaction {kwargs}')
        logging.info("----------------------------------------")
        return call_result.StopTransactionPayload()

    @on('StatusNotification')
    def on_status_notification(self, connector_id, error_code, status, **kwargs):
        if(error_code == ChargePointErrorCode.no_error):
            logging.info(f'CP status: {status} ({error_code})')
            cp.connector1_status = status
        else:
            logging.error(f'CP status: {status} ({error_code})')

        return call_result.StatusNotificationPayload()

async def on_connect(websocket, path):
    """ For every new charge point that connects, create a ChargePoint instance
    and start listening for messages.

    """
    charge_point_id = path.strip('/')
    cp = ChargePoint(charge_point_id, websocket)

    logging.info("Starting tests")

    await asyncio.gather(
        test_runner(cp),
        cp.start()
    )

async def main():
    logging.basicConfig(level=logging.WARNING)
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
