# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c"
				   "uart1.c"
				   "adc_control.c"
				   "mqtt_demo.c"
				   "sessionHandler.c"
				   "audioBuzzer.c"
				   )
#set(COMPONENT_SRCS "uart.c")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
