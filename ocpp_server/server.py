import asyncio
import websockets
from datetime import datetime, timezone

from ocpp.routing import on
from ocpp.v16 import ChargePoint as cp
from ocpp.v16 import call_result, call
import random

async def call_runner(cp):
    while True:
        await asyncio.sleep(2)
        print('calling ChangeAvailability')
        result = await cp.call(
            call.ChangeAvailabilityPayload(
                connector_id=random.randint(0,999), 
                type='Operative')
        )

        print(result)

class ChargePoint(cp):
    @on('BootNotification')
    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        print("replying to on boot msg")
        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=10,
            status='Accepted'
        )
        
    @on('Heartbeat')
    def on_heartbeat(self, **kwargs):
        print("replying to heartbeat")

        time = datetime.utcnow().replace(tzinfo=timezone.utc).isoformat()
        time = datetime.utcnow().isoformat() + 'Z'

        return call_result.HeartbeatPayload(
            current_time=time
        )

    @on('Authorize')
    def on_authorize_request(self, id_tag):
        print(f'authorizing {id_tag}')
        return call_result.AuthorizePayload(
            dict(expiry_date='2021-01-01', status='Accepted')
        )

async def on_connect(websocket, path):
    """ For every new charge point that connects, create a ChargePoint instance
    and start listening for messages.

    """
    charge_point_id = path.strip('/')
    cp = ChargePoint(charge_point_id, websocket)

    print("starting ocpp handler")
    
    caller = asyncio.create_task(call_runner(cp))

    await cp.start()

async def main():
    server = await websockets.serve(
        on_connect,
        '0.0.0.0',
        9000,
        subprotocols=['ocpp1.6']
    )

    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())