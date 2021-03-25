# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES "esp_adc_cal")
#set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "adc_control.c")

set(COMPONENT_ADD_INCLUDEDIRS "./")

register_component()
