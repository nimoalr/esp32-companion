idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "touch.c"
        "raster.c"
        "eyes.c"
        "anim.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
        esp_psram
        esp_hw_support
)
