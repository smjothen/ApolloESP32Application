
idf_component_register(SRCS "apollo_ota.c"
                  "ota_location.c" "crc32.c" "pic_update.c" "ota_command.c" "ota_log.c" "segmented_ota.c"  "safe_ota.c"
                  INCLUDE_DIRS "./include"
                  REQUIRES esp_https_ota app_update esp_http_client json zaptec_protocol zaptec_cloud ble i2c
                  EMBED_TXTFILES ${project_dir}/components/apollo_ota/certs/ca_cert.pem
                  EMBED_FILES ${project_dir}/bin/dspic.bin

		  REQUIRES ocpp
)
