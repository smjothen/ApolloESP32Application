idf_component_register(SRCS "network.c" "stdin_out.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_netif nvs_flash
                    )
