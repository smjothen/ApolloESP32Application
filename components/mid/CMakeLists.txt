idf_component_register(
	SRCS mid.c mid_sign.c
	REQUIRES json zaptec_protocol zaptec_cloud
	INCLUDE_DIRS .
)
