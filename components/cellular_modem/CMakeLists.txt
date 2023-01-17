idf_component_register(SRCS "ppp_task.c"
                            "at_commands.c"
									REQUIRES driver esp_netif
                  INCLUDE_DIRS "./include"
)
