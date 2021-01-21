set(COMPONENT_REQUIRES "audioBuzzer"
					   "authentication")

set(COMPONENT_SRCS  "i2cDevices.c"
					"i2cInterface.c"
					"SHT30.c"
					"RTC.c"
					"EEPROM.c"
					"CLRC661.c"
					"eeprom_wp.c"
					)

set(COMPONENT_ADD_INCLUDEDIRS ". include")

register_component()
