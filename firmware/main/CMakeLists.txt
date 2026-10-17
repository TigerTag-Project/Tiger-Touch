idf_component_register(
    SRCS
        "main.c"
        "settings.c"
        "demo_mode.c"
        "i18n.c"
        "net/wifi_mgr.c"
        "net/tz.c"
        "net/cloud.c"
        "net/http_lite.c"
        "net/ota.c"
        "drivers/drivers.c"
        "drivers/links.c"
        "drivers/k2_client.c"
        "drivers/drv_bambu.c"
        "drivers/drv_prusa.c"
        "drivers/drv_anycubic.c"
        "drivers/drv_klipper.c"
        "drivers/drv_octoprint.c"
        "drivers/drv_flashforge.c"
        "drivers/drv_elegoo.c"
        "drivers/drv_elegoo_sdcp.c"
        "drivers/drv_snapmaker.c"
        "drivers/ws_rx.c"
        "drivers/mqtt_rx.c"
        "ui/screen.c"
        "ui/ui_setup.c"
        "ui/ui_settings.c"
        "ui/ui_onboard.c"
        "ui/ui_dashboard.c"
        "ui/ui_wifi.c"
        "ui/ui_cards.c"
        "ui/ui_job.c"
        "ui/brand_logos.c"
        "ui/pictos.c"
        "ui/ui_tile.c"
        "ui/boot_splash.c"
        "ui/fonts/font_ui_14.c"
        "ui/fonts/font_ui_16.c"
        "ui/fonts/font_ui_20.c"
        "ui/fonts/font_ui_24.c"
        "ui/fonts/font_icon_32.c"
    # The folders are for people: a header is found by its bare name from any of
    # them, the way it always was.
    INCLUDE_DIRS "." "drivers" "net" "ui"
    REQUIRES PandaTouch_IDF lvgl
    PRIV_REQUIRES
        esp_wifi esp_event esp_netif esp_timer nvs_flash esp_app_format
        esp_http_client esp-tls mbedtls json driver app_update esp_partition espressif__esp_websocket_client mqtt
)
# The warnings of the CI build, written out. They come from ESP-IDF's own project
# setup, which is not the same on every machine: a build that does not ask for them
# says "no warnings" and a build that does fails. With them here, this component
# is built the same way everywhere.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Werror=all -Wextra
    -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable
    -Wno-error=deprecated-declarations -Wno-error=extra
    -Wno-unused-parameter -Wno-sign-compare -Wno-enum-conversion)
# the borrowed fonts are written against the "simple" include style
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
