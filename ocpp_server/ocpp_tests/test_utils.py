#!/usr/bin/env python3

import logging
import asyncio

from ocpp.routing import on
from ocpp.v16.enums import ConfigurationStatus
from ocpp.v16 import ChargePoint as cp
from ocpp.v16 import call

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
