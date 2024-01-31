idf_component_register(
	SRCS mid.c mid_event.c
	REQUIRES json zaptec_cloud zaptec_protocol crc16 lts
	INCLUDE_DIRS include
)
