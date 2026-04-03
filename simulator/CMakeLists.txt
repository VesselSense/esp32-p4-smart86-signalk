cmake_minimum_required(VERSION 3.16)
project(lvgl_sim C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

# ---------------------------------------------------------------------------
# LVGL 8.4.0 — same major version as esp-brookesia v0.2.x uses
# ---------------------------------------------------------------------------
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE 1 CACHE BOOL "" FORCE)

FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v8.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)

# LVGL 8.4 custom.cmake always adds examples/demos — exclude them since our
# minimal lv_conf.h doesn't enable all the widgets they reference.
set_target_properties(lvgl_examples PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(lvgl_demos    PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---------------------------------------------------------------------------
# stb_image_write — single-header PNG encoder
# ---------------------------------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ---------------------------------------------------------------------------
# Simulator executable
# Compiles the actual firmware UI code (signalk_app.cpp) against stubs for
# esp-ui, signalk_client, esp_log, esp_timer, sdkconfig, and FreeRTOS.
# ---------------------------------------------------------------------------
add_executable(lvgl_sim
    main.cpp
    ../components/instrument_ui/instrument_helpers.cpp
    ../components/autopilot_app/autopilot_app.cpp
    ../components/autopilot_app/lv_font_roboto_96.c
    ../components/wind_rose_app/wind_rose_app.cpp
    ../components/wind_rose_app/lv_font_montserrat_76.c
    ../components/wind_rose_app/lv_font_montserrat_62.c
    ../components/wind_rose_app/lv_font_montserrat_67.c
    ../components/wind_rose_app/lv_font_montserrat_bold_50.c
    ../components/data_browser_app/data_browser_app.cpp
    ../components/data_browser_app/lv_font_montserrat_90.c
    ../components/data_browser_app/lv_font_montserrat_120.c
    ../components/data_browser_app/lv_font_montserrat_310.c
    ../components/data_browser_app/lv_font_montserrat_360.c
    stubs/esp_ui_stub.cpp
    stubs/signalk_client_stub.cpp
    stubs/signalk_auth_stub.cpp
    stubs/wifi_manager_stub.cpp
    ../components/unit_config/unit_config.cpp
    ../components/settings_app/settings_app.cpp
    ../components/settings_app/settings_gear_icon.c
    ../components/autopilot_app/helm_icon.c
    ../components/wifi_settings_app/wifi_settings_app.cpp
    ../components/wifi_settings_app/wifi_icon.c
    stubs/esp_ui_phone_app_launcher_image_default.c
)

target_link_libraries(lvgl_sim PRIVATE lvgl m)
target_compile_definitions(lvgl_sim PRIVATE SIMULATOR_BUILD=1)

target_include_directories(lvgl_sim PRIVATE
    # Stubs FIRST — shadow esp_log.h, esp_timer.h, esp_ui.hpp, sdkconfig.h, freertos/
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    # Firmware component headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/data_browser_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/instrument_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/autopilot_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/wind_rose_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/unit_config
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/settings_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/wifi_settings_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/wifi_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/signalk_client
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/signalk_auth
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/audio_feedback
    # stb and lv_conf.h
    ${stb_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    # lvgl target already exposes its root via PUBLIC include dirs
)

# Suppress warnings from stb headers and minor C/C++ cross-compile noise
target_compile_options(lvgl_sim PRIVATE
    -Wno-unused-function
    -Wno-missing-field-initializers
    -Wno-deprecated-declarations
)
