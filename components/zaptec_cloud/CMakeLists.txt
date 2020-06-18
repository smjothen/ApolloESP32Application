
idf_component_register(SRCS 
                      "cloud_listener.c"
                      "sas_token.c"
                      "zaptec_cloud_observations.c"
                      "wpa_supplicant/base64.c"
                      "wpa_supplicant/sha256.c"
                      "wpa_supplicant/common.c"
                  INCLUDE_DIRS "./include" "./include/wpa_supplicant"
                  REQUIRES json mqtt)    