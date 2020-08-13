
idf_component_register(SRCS "apollo_ota.c"
                  "ota_location.c"
                  INCLUDE_DIRS "./include"
                  REQUIRES esp_https_ota app_update esp_http_client json
                  EMBED_TXTFILES ${project_dir}/components/apollo_ota/certs/ca_cert.pem
                  EMBED_FILES ${project_dir}/bin/dspic.bin
)