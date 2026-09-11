idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "touch.c"
        "raster.c"
        "eyes.c"
        "eye_symbols.c"
        "dance_fill.c"
        "dance_lasers.c"
        "anim.c"
        "play.c"
        "glass.c"
        "sfx.c"
        "i2c_bus.c"
        "imu.c"
        "pmic.c"
        "power.c"
        "locked_sleep.c"
        "lock_policy.c"
        "lock_screen.c"
        "settings.c"
        "battstat.c"
        "gfx.c"
        "font_spleen_8x16.c"
        "font_spleen_12x24.c"
        "font_spleen_16x32.c"
        "imu_cal.c"
        "ui.c"
        "audio.c"
        "rhythm_rush.c" "music_evidence.c"
        "music_trace.c"
        "micdir.c"
        "voice.c"
        "speech.c"
        "persona.c"
        "adpcm.c"
        "clips_gen.c"
        "accessories.c"
        "behavior.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        vfs
        esp_driver_usb_serial_jtag
        esp_app_format
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
        esp_psram
        esp_hw_support
        esp_pm
        nvs_flash
        esp_driver_i2s
)

# Task stacks are deliberately small. Catch oversized automatic frame/job arrays
# at build time; persistent render buffers must not live on a task stack.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=4096)
