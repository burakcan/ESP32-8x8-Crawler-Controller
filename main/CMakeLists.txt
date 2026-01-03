# Get build timestamp (YYMMDD.HHMM format, e.g., 251218.1430)
# This runs at build time, not configure time
string(TIMESTAMP BUILD_DATE "%y%m%d.%H%M")

# Get short git hash for reference
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_SHORT_HASH)
    set(GIT_SHORT_HASH "unknown")
endif()

idf_component_register(
    SRCS
        "main.c"
        "nvs_storage.c"
        "rc_input.c"
        "pwm_output.c"
        "calibration.c"
        "tuning.c"
        "web_server.c"
        "ota_update.c"
        "led_rgb.c"
        "udp_log.c"
        "sound.c"
        "engine_sound.c"
        "sounds/sound_profiles.c"
    INCLUDE_DIRS "." "sounds" "sounds/tatra813" "sounds/detroit8v92" "sounds/cat3408"
    REQUIRES
        driver
        esp_driver_mcpwm
        esp_driver_rmt
        esp_driver_i2s
        nvs_flash
        esp_timer
        esp_wifi
        esp_http_server
        esp_netif
        esp_event
        spiffs
        app_update
        esp_app_format
        mdns
)

# Add compile definitions to this component
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FW_BUILD_DATE="${BUILD_DATE}"
    FW_GIT_HASH="${GIT_SHORT_HASH}"
)