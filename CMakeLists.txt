# JC8012P4A1_BSP_ESP32P4 - Smart Control Panel UI
#
# Guition JC8012P4A1 : ESP32-P4NRW32 + ESP32-C6, 10.1" 800x1280 MIPI-DSI (JD9365),
# GSL3680 capacitive touch, ES8311 codec, TF card, RX8025T RTC.
#
# Built against ESP-IDF v5.3 (also builds on v5.4 / v5.5).
# Fully offline: every dependency is vendored under components/, there is no
# idf_component.yml anywhere in the tree, so the component manager never
# contacts components.espressif.com.

cmake_minimum_required(VERSION 3.16)

# Belt and braces: even if a stray manifest shows up, never go to the network.
set(ENV{IDF_COMPONENT_MANAGER} "0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The vendored GSL3680 firmware blob and the LVGL sources are third-party and
# are not warning-clean under IDF's default flags.
add_compile_options(-Wno-format -Wno-unused-function -Wno-ignored-qualifiers)

project(JC8012P4A1_BSP_ESP32P4)
