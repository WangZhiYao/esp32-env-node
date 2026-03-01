idf_component_register(
    SRCS "app_espnow.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi app_storage app_protocol 
)