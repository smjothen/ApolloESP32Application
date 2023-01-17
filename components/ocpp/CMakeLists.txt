
idf_component_register(SRCS "ocpp_task.c"
                  INCLUDE_DIRS "./include"
                  REQUIRES driver esp_wifi esp_event esp_websocket_client json)
