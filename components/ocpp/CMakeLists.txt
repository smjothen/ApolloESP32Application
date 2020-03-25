
idf_component_register(SRCS "ocpp_task.c"
                  INCLUDE_DIRS "./include"
                  REQUIRES esp_websocket_client json button)