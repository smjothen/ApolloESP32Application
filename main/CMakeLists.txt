# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c"
				   "adc_control.c"
				   "mqtt_demo.c"
				   "sessionHandler.c"
				   "audioBuzzer.c"
				   "production_test.c"
				   "storage.c"
				   "network.c"
				   )

set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
