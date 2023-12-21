idf_component_register(
	SRCS calibration.c calibration_voffs.c calibration_vgain.c calibration_ioffs.c calibration_igain.c calibration_verify.c calibration_crc.c calibration_util.c calibration_emeter.c calibration_https.c calibration-message.pb.c
	REQUIRES wifi i2c mid nanopb
	INCLUDE_DIRS . include
)
