# Edit following two lines to set component requirements (see docs)
#set(COMPONENT_REQUIRES "wpa_supplicant")
#set(COMPONENT_REQUIRES )


#set(COMPONENT_PRIV_REQUIRES )

#set(COMPONENT_SRCS "main.c"
#				   "sessionHandler.c"
#				   "production_test.c"
#				   "storage.c"
#				   "connectivity.c"
#				   "chargeSession.c"
#				   "certificate.c"
#				   "fat.c"
				   #"diagnostics_port.c"
#				   )

#set(COMPONENT_ADD_INCLUDEDIRS "")

#register_component()

idf_component_register(SRCS 
					"main.c"
				   	"sessionHandler.c"
				   	"production_test.c"
				   	"storage.c"
				   	"connectivity.c"
				   	"chargeSession.c"
				   	"certificate.c"
				   	"fat.c"	
				   	"diagnostics_port.c"
					EMBED_TXTFILES ${project_dir}/main/cert/zaptec_ca.cer
					EMBED_TXTFILES ${project_dir}/main/cert/bundle7.crt 

					INCLUDE_DIRS "./"
					)
