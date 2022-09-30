set(COMPONENT_SRCS 
	calibration.c

	calibration_voffs.c
	calibration_vgain.c
	calibration_ioffs.c
	calibration_igain.c

	calibration_crc.c
	calibration_util.c
	calibration_emeter.c

	calibration-message.pb.c
	pb_common.c
	pb_encode.c
	pb_decode.c
)

set(COMPONENT_REQUIRES wifi i2c)

set(COMPONENT_ADD_INCLUDEDIRS ". include")

register_component()
