#!/usr/bin/env python3
from enum import Enum

import traceback
import logging
import asyncio
import serial_asyncio
import time
from typing import Optional
from ocpp.routing import on
from ocpp.v16.enums import ConfigurationStatus, Action
from ocpp.v16 import ChargePoint as cp
from ocpp.v16 import call

class OperationState(Enum):
    disconnected = "A"
    connected = "B"
    charging = "C"

class MCUWarning(Enum):
    WARNING_HUMIDITY = 0
    WARNING_TEMPERATURE = 1
    WARNING_TEMPERATURE_ERROR = 2
    WARNING_OVERCURRENT_INSTALLATION = 20
    WARNING_UNEXPECTED_RELAY = 21
    WARNING_O_PEN = 22
    WARNING_FPGA_WATCHDOG = 28
    WARNING_NO_SWITCH_POW_DEF = 9
    WARNING_EMETER_NO_RESPONSE = 3
    WARNING_EMETER_LINK = 25
    WARNING_EMETER_ALARM = 24
    WARNING_CHARGE_OVERCURRENT = 5
    WARNING_NO_VOLTAGE_L1 = 26
    WARNING_NO_VOLTAGE_L2_L3 = 27
    WARNING_MAX_SESSION_RESTART = 4
    WARNING_12V_LOW_LEVEL = 7
    WARNING_PILOT_STATE = 6
    WARNING_PILOT_LOW_LEVEL = 8
    WARNING_PILOT_NO_PROXIMITY = 23
    WARNING_REBOOT = 10
    WARNING_DISABLED = 11
    WARNING_FPGA_VERSION = 31
    WARNING_RCD_6MA = 12
    WARNING_RCD_30MA = 13
    WARNING_RCD_TEST_6MA = 16
    WARNING_RCD_TEST_30MA = 17
    WARNING_RCD_FAILURE = 18
    WARNING_SERVO = 29
    WARNING_MID = 30

class ZapClientInput(asyncio.Protocol):
    def __init__(self):
        self.connected = False
        self.enabled = False

    def connection_made(self, transport):
        self.transport = transport
        self.connnected = True
        print('port opened', transport)

    def data_received(self, data):
        logging.info(f'data received {str(data)}')
        if data == b'\x00':
            logging.info("MCU reboot detected")
            self.enabled = False
        elif str(data).endswith('Apollo:>"') or str(data).endswith('Apollo:>') or str(data).endswith("Apollo:>'"):
            logging.info("Ready for command")
        else:
            logging.info(f"Not sure if ready: {str(data)[-8:]}")

    def connection_lost(self, exc):
        logging.info('port closed')
        self.transport.loop.stop()

    def pause_writing(self):
        logging.info('pause writing')
        logging.info(self.transport.get_write_buffer_size())

    def resume_writing(self):
        logging.info(self.transport.get_write_buffer_size())
        logging.info('resume writing')

class ZapClientOutput():
    def __init__(self, transport):
        self.transport = transport
        self.enabled = False

    def enable(self):
        self.transport.write(b'zapclienable')  # Write serial data via transport
        self.enabled = True

    def attempt_ready(self):
        self.write("")

    def write(self, message: str):
        logging.info(f"writing: {message}")
        if not self.enabled:
            self.enable()

        self.transport.write(bytearray(message.encode('utf-8')))
        self.transport.write(b'\r\n')

    def set_enabled(self, enabled: bool):
        self.enabled = enabled

    def set_car_state(self, state: OperationState):
        self.write(f'car {state.value}')

    def set_warning(warning: MCUWarning):
        self.write(f'warn {warning}')

async def ensure_configuration(cp, key_value_pairs: dict):
    try:
        result = await cp.call(call.GetConfigurationPayload(None))
        if(result == None):
            logging.info("Unable to get configuration to prepare test")
            return -1
    except Exception as e:
        logging.info(f"Could not get configuration to ensure state: {e}")
        return -1

    tmp = dict(key_value_pairs)
    for key in key_value_pairs:
        for current_config_entry in result.configuration_key:
            if key == current_config_entry["key"]:
                if key_value_pairs[key] != current_config_entry["value"]:
                    logging.info(f'Ensuring {key} = {key_value_pairs[key]}')
                    change_res = await cp.call(call.ChangeConfigurationPayload(key, key_value_pairs[key]))
                    logging.info(change_res)
                    if(result == None or change_res.status != ConfigurationStatus.accepted):
                        logging.error(f"Unable to configure {key} to {key_value_pairs[key]}")
                        return -1
                    tmp.pop(key)
                else:
                    tmp.pop(key)

    if tmp:
        logging.error(f"Some values were not found on charger: {key_value_pairs}")
        return -1
    else:
        return 0

async def wait_for_cp_action_event(cp, action: Action, timeout: Optional[int] = None):
    await asyncio.wait_for(cp.action_events[action].wait(), timeout)
    cp.action_events[action].clear()

async def wait_for_cp_status(cp, states: list, timeout: Optional[int] = None):
    start_time = time.time()
    while(cp.connector1_status not in states):
        elapsed = int(time.time() - start_time)

        wait = None
        if timeout is None:
            logging.warning(f"Waiting for status {states}...({cp.connector1_status})")
        else:
            if elapsed >= timeout:
                break

            wait = timeout - elapsed
            logging.warning(f"Waiting up to {wait} seconds for status {states}...({cp.connector1_status})")
        try:
            await wait_for_cp_action_event(cp, Action.StatusNotification, wait)
        except TimeoutError:
            logging.error("Timed out")
            break
        except Exception as e:
            logging.error(f"Exception occured while waiting for status: {e}")
            traceback.print_exc()
            break

    return cp.connector1_status
