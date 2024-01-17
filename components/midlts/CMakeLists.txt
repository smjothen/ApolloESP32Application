idf_component_register(SRCS "mid_lts.c" "mid_lts_stress.c" "mid_lts_format.c" "mid_session.pb.c" "mid_sign.c" "mid_ocmf.c"
                       INCLUDE_DIRS "include" "."
					   REQUIRES littlefs nanopb utz mbedtls json)
