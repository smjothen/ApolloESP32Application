idf_component_register(SRCS "network.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_netif nvs_flash vfs driver esp_wifi
                    )
