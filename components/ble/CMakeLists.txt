idf_component_register(
	SRCS ble_common.c ble_gap.c ble_gatt_server.c ble_interface.c ble_service_wifi_config.c
	REQUIRES driver nvs_flash bt json esp_websocket_client
	INCLUDE_DIRS .
)
