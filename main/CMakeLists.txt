idf_component_register(SRCS "main.c" "soil_moisture.c" "sd_card.c"
                       PRIV_REQUIRES esp_adc fatfs sdmmc esp_driver_sdspi esp_driver_spi
                       INCLUDE_DIRS ".")
