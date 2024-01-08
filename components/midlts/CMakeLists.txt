idf_component_register(
	SRCS mid_sign.c mid_lts.c mid_log.c mid_ocmf.c
	# NOTE: Don't add dependencies unless they are modular or from the ESP-IDF
	# so we can unit test this module.
	REQUIRES json mbedtls vfs fatfs utz
	INCLUDE_DIRS include
)
