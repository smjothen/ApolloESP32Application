# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES "bt" json)
#set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "ble_common.c"
				   "ble_gap.c"
				   "ble_gatt_server.c"
				   "ble_interface.c"
				   "ble_service_wifi_config.c"
				   )

set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
