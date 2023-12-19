idf_component_register(
	SRCS emclog.c emclog_fields.c
	REQUIRES driver esp_wifi esp_partition utz zaptec_protocol wifi i2c
	INCLUDE_DIRS .
)
