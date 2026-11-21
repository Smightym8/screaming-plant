idf_component_register(SRCS "main.c" "soil_moisture.c" "soil_monitor.c" "sd_card.c" "audio_player.c" "playlist.c" "wav.c"
                       PRIV_REQUIRES esp_adc fatfs sdmmc esp_driver_sdspi esp_driver_spi
                                     esp_driver_i2s bootloader_support
                       INCLUDE_DIRS ".")
