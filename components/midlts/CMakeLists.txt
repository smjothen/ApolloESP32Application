idf_component_register(SRCS "mid_lts.c" "mid_lts_stress.c" "mid_lts_format.c" "mid_sign.c"
                       INCLUDE_DIRS "include"
                       REQUIRES littlefs mbedtls json)
