idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "raster.c"
        "eyes.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
        esp_timer
        esp_psram
        esp_hw_support
)
