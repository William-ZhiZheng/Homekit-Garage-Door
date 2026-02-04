idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server esp_wifi nvs_flash app_update esp_hap_core garage_door)