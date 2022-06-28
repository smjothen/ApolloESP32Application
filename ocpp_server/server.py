import asyncio
import websockets
import time
import datetime
from datetime import datetime, timezone

from ocpp.routing import on
from ocpp.v16 import ChargePoint as cp
from ocpp.v16 import call_result, call
import random

config_keys=['AllowOfflineTxForUnknownId',
             'AuthorizationCacheEnabled',
             'AuthorizeRemoteTxRequests',
             'BlinkRepeat',
             'ClockAlignedDataInterval',
             'ConnectionTimeOut',
             'ConnectorPhaseRotation',
             'ConnectorPhaseRotationMaxLength',
             'GetConfigurationMaxKeys',
             'HeartbeatInterval',
             'LightIntensity',
             'LocalAuthorizeOffline',
             'LocalPreAuthorize',
             'MaxEnergyOnInvalidId',
             'MeterValuesAlignedData',
             'MeterValuesAlignedDataMaxLength',
             'MeterValuesSampledData',
             'MeterValuesSampledDataMaxLength',
             'MeterValueSampleInterval',
             'MinimumStatusDuration',
             'NumberOfConnectors',
             'ResetRetries',
             'StopTransactionOnEVSideDisconnect',
             'StopTransactionOnInvalidId',
             'StopTxnAlignedData',
             'StopTxnAlignedDataMaxLength',
             'StopTxnSampledData',
             'StopTxnSampledDataMaxLength',
             'SupportedFeatureProfiles',
             'SupportedFeatureProfilesMaxLength',
             'TransactionMessageAttempts',
             'TransactionMessageRetryInterval',
             'UnlockConnectorOnEVSideDisconnect',
             'WebSocketPingInterval',# end of core profile
             'LocalAuthListEnabled',
             'LocalAuthListMaxLength',
             'SendLocalListMaxLength',
             'ReserveConnectorZeroSupported',
             'ChargeProfileMaxStackLevel',
             'ChargingScheduleAllowedChargingRateUnit',
             'ChargingScheduleMaxPeriods',
             'ConnectorSwitch3to1PhaseSupported',
             'MaxChargingProfilesInstalled']

async def call_runner(cp):
    await asyncio.sleep(2)

    print('Changing phase rotation (valid)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='ConnectorPhaseRotation',
            value='0.RST, 1.RST')
    )
    print(result)

    print('calling get configuration for phase rotation')
    result = await cp.call(
        call.GetConfigurationPayload(
            key=['ConnectorPhaseRotation'])
    )
    print(result)

    print('Changing clock aligned interval (valid uint32)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='ClockAlignedDataInterval',
            value='0')
    )
    print(result)

    print('Changing sample interval (valid uint32)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='MeterValueSampleInterval',
            value='10')
    )
    print(result)

    print('Changing MeterValuesAlignedData (valid csl)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='MeterValuesAlignedData',
            value='Current.Import, Current.Offered, Energy.Active.Import.Interval, Power.Active.Import, Temperature, Voltage')
    )
    print(result)

    print('Changing MeterValuesSampledData (valid csl)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='MeterValuesSampledData',
            value='Current.Import, Current.Offered, Energy.Active.Import.Interval, Power.Active.Import, Temperature, Voltage')
    )
    print(result)

    print('Changing StopTxnAlignedData (valid csl)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='StopTxnAlignedData',
            value='Energy.Active.Import.Interval')
    )
    print(result)

    print('Changing StopTxnSampledData (valid csl)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='StopTxnSampledData',
            value='Current.Import, Current.Offered, Energy.Active.Import.Interval, Power.Active.Import, Temperature, Voltage')
    )
    print(result)

    print('Enabling LocalAuthListEnabled')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='LocalAuthListEnabled',
            value='true')
    )
    print(result)

    print('Enabling local pre authorization ')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='LocalPreAuthorize',
            value='true')
    )
    print(result)

    print('Adding known test tags')
    result = await cp.call(
        call.SendLocalListPayload(
            list_version = 12,
            local_authorization_list = [{'idTag' : 'nfc-0307A5CC',
                                         'idTagInfo' : {'parentIdTag' : "other",
                                                        'status' : 'Accepted'}},
                                        {'idTag' : 'nfc-73F776CC',
                                         'idTagInfo' : {'parentIdTag' : "fd65bbe2-edc8-4940-9",
                                                        'status' : 'Accepted'}}
                                        ],
            update_type = 'Differential')
    )
    print(result)

    print('Attempting remote start transaction')
    try:
        result = await cp.call(
            call.RemoteStartTransactionPayload(
                connector_id=1,
                id_tag ="nfc-73F776CC"
            )
        )
        print(result)
    except Exception as e:
        print(e)

    await asyncio.sleep(10)
    print('Attempting remote stop transaction')

    try:
        result = await cp.call(
            call.RemoteStopTransactionPayload(
                transaction_id=1231312,
            )
        )
        print(result)
    except Exception as e:
        print(e)
    return

    try:
        print('unlocking connector (expect not supported)')
        result = await cp.call(
            call.ClearCachePayload()
        )
        print(result)
    except Exception as e:
        print(e)

    print('clearing authorization cach (expect not supported)')
    result = await cp.call(
        call.UnlockConnectorPayload(
            connector_id = 1)
    )
    print(result)

    print('Starting data transfer')
    result = await cp.call(
        call.DataTransferPayload(
            vendor_id='com.zaptec')
    )
    print(result)

    for x in range(5) :
        await asyncio.sleep(2)
        print('calling ChangeAvailability')
        result = await cp.call(
            call.ChangeAvailabilityPayload(
                connector_id=random.randint(0,999),
                type='Operative')
        )

        print(result)
        print('calling ReserveNow')
        result = await cp.call(
            call.ReserveNowPayload(
                connector_id=random.randint(0,999),
                expiry_date='2021-01-01',
                id_tag='73f776cc',
                reservation_id=53)
        )
        print(result)
#{'idTag' = 'e0752f98-3bed-4386-bf57-7d783afe9013', 'idTagInfo' = {'expiryDate' = '2021-01-01', 'parentIdTag' = "fd65bbe2-edc8-4940-99e7-17832961684d", 'status' = 'Accepted'}}
    print('Changing LocalAuthListEnabled')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='LocalAuthListEnabled',
            value='true')
    )
    print(result)

    print('calling sendLocalList with list')
    try:
        result = await cp.call(
            call.SendLocalListPayload(
                list_version = 1,
                local_authorization_list = [{'idTag' : 'e0752f98-3bed-4386-b',
                                             'idTagInfo' : {'expiryDate' : datetime.now().isoformat(),
                                                            'parentIdTag' : "fd65bbe2-edc8-4940-9",
                                                            'status' : 'Accepted'}}],
                update_type = 'Full')
        )
        print(result)
    except Exception as e:
        print(e)

    print('calling sendLocalList with empty differential list')
    result = await cp.call(
        call.SendLocalListPayload(
            list_version = 2,
            update_type = 'Differential')
    )
    print(result)

    print('calling sendLocalList with differential list old version (expect version mismatch)')
    result = await cp.call(
        call.SendLocalListPayload(
            list_version = 2,
            update_type = 'Differential')
    )
    print(result)

    print('calling sendLocalList with differential list updating existing and adding new')
    result = await cp.call(
        call.SendLocalListPayload(
            list_version = 3,
            local_authorization_list = [{'idTag' : 'e0752f98-3bed-4386-b',
                                         'idTagInfo' : {'parentIdTag' : "fd65bbe2-edc8-4940-9",
                                                        'status' : 'Expired'}},
                                        {'idTag' : '961299e7-b2ce-4231-b',
                                         'idTagInfo' : {'parentIdTag' : "fd65bbe2-edc8-4940-9",
                                                        'status' : 'Accepted'}}
                                        ],
            update_type = 'Differential')
    )
    print(result)

    print('calling sendLocalList with differential list with shortform delete')
    result = await cp.call(
        call.SendLocalListPayload(
            list_version = 4,
            local_authorization_list = [{'idTag' : 'e0752f98-3bed-4386-b'}],
            update_type = 'Differential')
    )
    print(result)

    try:
        import keys
        print('calling sendLocalList with Full with many keys')
        result = await cp.call(
            call.SendLocalListPayload(
                list_version = 1,
                local_authorization_list = keys.key_list,
                update_type = 'Full')
        )
        print(result)
    except Exception as e:
        print(e)
    #print('calling bad reset')
    #result = await cp.call(
    #    call.ResetPayload(
    #        type='other')
    #)
    #print(result)

    print('calling soft reset')
    result = await cp.call(
        call.ResetPayload(
            type='Soft')
    )
    print(result)

    # print('calling hard reset')
    # result = await cp.call(
    #     call.ResetPayload(
    #         type='Hard')
    # )
    # print(result)

    # print('Changing authorize remote tx (valid bool)')
    # result = await cp.call(
    #     call.ChangeConfigurationPayload(
    #         key='AuthorizeRemoteTxRequests',
    #         value='false')
    # )
    # print(result)
    # time.sleep(1)

    print('Changing reset retries (valid uint8)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='LightIntensity',
            value='5')
    )
    print(result)
    time.sleep(1)

    print('Changing transaction retry (valid uint16)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='TransactionMessageAttempts',
            value='25')
    )
    print(result)
    time.sleep(1)

    print('Changing feature profiles (invalid Supported)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='SupportedFeatureProfiles',
            value='core,FirmwareManagement')
    )
    print(result)
    time.sleep(1)

    print('Changing ReserveConnectorZeroSupported (invalid NotSupported)')
    result = await cp.call(
        call.ChangeConfigurationPayload(
            key='ReserveConnectorZeroSupported',
            value='true')
    )
    print(result)
    time.sleep(1)

    for attemp in range(0,5):
        print('calling get configuration for SupportedFeatureProfile')
        result = await cp.call(
            call.GetConfigurationPayload(
                key=['SupportedFeatureProfiles'])
        )
        print(result)
        time.sleep(1)

    for attemp in range(0,5):
        print('calling get configuration for invalid key')
        result = await cp.call(
            call.GetConfigurationPayload(
                key=['Not_an_actual_config_key'])
        )
        print(result)
        time.sleep(1)

    for attemp in range(0,5):
        print('calling get configuration for invalid and valid key')
        result = await cp.call(
            call.GetConfigurationPayload(
                key=['SupportedFeatureProfiles', 'Not_an_actual_config_key'])
        )
        print(result)
        time.sleep(1)

    for attempt in range(0,5):
        keys = random.sample(config_keys, random.randrange(0,len(config_keys)))
        print(f'calling get configuration random config keys: {keys}')
        result = await cp.call(
            call.GetConfigurationPayload(
                key=keys)
        )
        print(result)
        print("\n")
        time.sleep(1)

    for attempt in range(0,5):
    # while True:
        print('calling get configuration for all known configurations')
        result = await cp.call(
            call.GetConfigurationPayload()
        )
        print(result)
        print("\n")
        time.sleep(1)

    time.sleep(5)

    # print('calling change configuration for heartbeat')
    # result = await cp.call(
    #     call.ChangeConfigurationPayload(
    #         key='HeartbeatInterval',
    #         value='1')
    # )
    # print(result)

    # time.sleep(5)

    # print('calling change configuration for heartbeat')
    # result = await cp.call(
    #     call.ChangeConfigurationPayload(
    #         key='HeartbeatInterval',
    #         value='4')
    # )
    # print(result)
    # time.sleep(10)

class ChargePoint(cp):
    @on('BootNotification')
    def on_boot_notitication(self, charge_point_vendor, charge_point_model, **kwargs):
        print(charge_point_vendor, charge_point_model, kwargs)
        print("replying to on boot msg")
        return call_result.BootNotificationPayload(
            current_time=datetime.utcnow().isoformat(),
            interval=120,
            status='Accepted'
        )

    @on('Heartbeat')
    def on_heartbeat(self, **kwargs):
        print("replying to heartbeat")

        time = datetime.utcnow().replace(tzinfo=timezone.utc).isoformat()
        time = datetime.utcnow().isoformat() + 'Z'

        return call_result.HeartbeatPayload(
            current_time=time,
        )

    @on('Authorize')
    def on_authorize_request(self, id_tag):
        print(f'authorizing {id_tag}')
        return call_result.AuthorizePayload(
            dict(expiry_date='2021-01-01', parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        )

    @on('StartTransaction')
    def on_start_transaction(self, connector_id, id_tag, meter_start, **kwargs):
        print('Replying to start transaction')
        info=dict(expiryDate='2021-01-01', parentIdTag='fd65bbe2-edc8-4940-9', status='Accepted')
        return call_result.StartTransactionPayload(
            id_tag_info=info,
            transaction_id=1231312
        )

    @on('MeterValues')
    def on_meter_value(self, **kwargs):
        print(f'Meter value: {kwargs}')
        return call_result.MeterValuesPayload()

    @on('StopTransaction')
    def on_stop_transaction(self, **kwargs):
        print("----------------------------------------")
        print(f'Replying to stop transaction {kwargs}')
        print("----------------------------------------")
        return call_result.StopTransactionPayload()

    @on('StatusNotification')
    def on_status_notification(self, connector_id, error_code, status, **kwargs):
        print(f'CP status: {status} ({error_code})')
        return call_result.StatusNotificationPayload()

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
        subprotocols=['ocpp1.6'],
    )

    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())
