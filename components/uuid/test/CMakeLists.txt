idf_component_register(SRCS "test_uuid.c"
                       INCLUDE_DIRS "."
                       REQUIRES cmock uuid test_utils esp_timer)
