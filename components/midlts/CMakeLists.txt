idf_component_register(
	SRCS mid_sign.c mid_lts.c
	# NOTE: Don't add dependencies unless they are modular or from the ESP-IDF
	# so we can unit test this module.
	REQUIRES json mbedtls
	INCLUDE_DIRS include
)
