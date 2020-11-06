#set(COMPONENT_REQUIRES "esp_modem")

idf_component_register(SRCS "ppp_task.c"
                            "at_commands.c"
                  INCLUDE_DIRS "./include"
)