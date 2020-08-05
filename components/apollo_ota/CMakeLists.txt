
idf_component_register(SRCS "apollo_ota.c"
                  INCLUDE_DIRS "./include"
                  REQUIRES esp_https_ota app_update
                  EMBED_TXTFILES ${project_dir}/components/apollo_ota/certs/ca_cert.pem
)