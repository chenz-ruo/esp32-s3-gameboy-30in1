idf_component_register(
    SRCS "main.c" "bsp.c" "gfx.c" "font_data.c" "app.c" "ui.c" "audio.c" "tracks.c"
     "games_reflex.c" "games_react.c" "games_misc.c" "games_batch2.c" "games_batch3.c" "games_quickfire.c"
     "vendor/es8311.c" "vendor/myi2s.c" "vendor/myiic.c"
    INCLUDE_DIRS "." "vendor"
    REQUIRES driver esp_driver_uart esp_driver_gpio esp_driver_i2s esp_lcd esp_timer nvs_flash
)
