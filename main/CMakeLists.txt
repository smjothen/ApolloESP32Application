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
				   	"OCMF.c"
					"offline_log.c"
					"offlineSession.c"
					"crc32.c"
					"IT3PCalculator.c"
					"offlineHandler.c"
					"ocpp.c"
					"chargeController.c"
					"efuse.c"
					EMBED_TXTFILES ${project_dir}/main/cert/zaptec_ca.cer
					EMBED_TXTFILES ${project_dir}/main/cert/bundle8.crt 

					INCLUDE_DIRS "./"
					)
