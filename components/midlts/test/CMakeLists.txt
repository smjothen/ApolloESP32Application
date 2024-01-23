idf_component_register(SRCS "test_log.c" "test_ocmf.c" "test_sign.c"
                       INCLUDE_DIRS "."
                       REQUIRES cmock midlts test_utils)
