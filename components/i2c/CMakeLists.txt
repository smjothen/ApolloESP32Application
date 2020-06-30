
set(COMPONENT_SRCS  "i2cDevices.c"
					"i2cInterface.c"
					"SHT30.c"
					"RTC.c"
					"EEPROM.c"
					"CLRC661.c")

set(COMPONENT_ADD_INCLUDEDIRS ". include")

register_component()
