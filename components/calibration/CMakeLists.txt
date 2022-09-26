set(COMPONENT_SRCS 
	calibration.c
	calibration-message.pb.c
	pb_common.c
	pb_encode.c
	pb_decode.c
	emeter.c
	)

set(COMPONENT_REQUIRES wifi i2c)

set(COMPONENT_ADD_INCLUDEDIRS ". include")

register_component()
