#!/usr/bin/env bash
#
# Re-download every vendored dependency into components/.
#
# Run this on a machine WITH network access. The result is committed so that
# the project itself builds offline. See docs/OFFLINE_BUILD.md.
#
# Usage:  tools/vendor_components.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPONENTS="${REPO_ROOT}/components"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# --- pinned versions -------------------------------------------------------
# namespace:name:version  (ESP Component Registry)
REGISTRY_PINS=(
    "lvgl:lvgl:9.2.2"
    "espressif:esp_lvgl_port:2.4.4"
    "espressif:esp_lcd_touch:1.1.2"
    "espressif:esp_codec_dev:1.3.4"
    "espressif:cmake_utilities:0.5.3"
)

# Guition vendor package: board drivers not published on the registry.
GUITION_RAW="https://raw.githubusercontent.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6/main/1-Demo/idf-examples/common_components"

JD9365_FILES=(
    "CHANGELOG.md" "CMakeLists.txt" "README.md"
    "esp_lcd_jd9365.c" "license.txt" "include/esp_lcd_jd9365.h"
)
GSL3680_FILES=(
    "CMakeLists.txt" "esp_lcd_touch_gsl3680.c" "gsl_point_id.c"
    "include/esp_lcd_touch_gsl3680.h" "include/gsl_point_id.h"
)

# LVGL directories removed to keep the repo small. env_support MUST stay:
# lvgl/CMakeLists.txt includes env_support/cmake/esp.cmake.
LVGL_PRUNE=(tests docs scripts zephyr CMakePresets.json)

say() { printf '\n==> %s\n' "$*"; }

# --- registry components ---------------------------------------------------
for pin in "${REGISTRY_PINS[@]}"; do
    IFS=':' read -r ns name ver <<< "${pin}"
    url="https://components-file.espressif.com/components/${ns}/${name}/${ver}/${ns}__${name}-v${ver}.zip"
    zip="${WORK}/${ns}__${name}.zip"

    say "${ns}/${name} @ ${ver}"
    curl -fSL --retry 3 --max-time 300 -o "${zip}" "${url}"

    dest="${COMPONENTS}/${name}"
    rm -rf "${dest}"
    mkdir -p "${dest}"
    unzip -q -o "${zip}" -d "${dest}"
done

# --- LVGL pruning ----------------------------------------------------------
say "pruning components/lvgl"
for d in "${LVGL_PRUNE[@]}"; do
    rm -rf "${COMPONENTS}/lvgl/${d:?}"
done
rm -rf "${COMPONENTS}/lvgl/demos" "${COMPONENTS}/lvgl/examples"

# Of env_support only cmake/ is needed; the rest is other build systems.
find "${COMPONENTS}/lvgl/env_support" -mindepth 1 -maxdepth 1 \
     ! -name cmake -exec rm -rf {} +

# esp.cmake passes these two to idf_component_register(INCLUDE_DIRS ...) and
# ESP-IDF errors on a missing include dir, so they have to exist even empty.
for d in demos examples; do
    mkdir -p "${COMPONENTS}/lvgl/${d}"
    cat > "${COMPONENTS}/lvgl/${d}/.gitkeep" <<EOF
LVGL bundled ${d}/ were removed from this vendored copy to keep the repo small.
This directory must exist because env_support/cmake/esp.cmake passes it to
idf_component_register(INCLUDE_DIRS ...), and ESP-IDF errors on a missing
include dir. Keep the matching CONFIG_LV_* options off (see sdkconfig.defaults).
EOF
done

# --- other trimming --------------------------------------------------------
rm -rf "${COMPONENTS}/esp_lvgl_port/test_apps" \
       "${COMPONENTS}/esp_lvgl_port/docs" \
       "${COMPONENTS}/esp_lvgl_port/examples" \
       "${COMPONENTS}/esp_lvgl_port/images/lvgl8" \
       "${COMPONENTS}/esp_codec_dev/test_apps" \
       "${COMPONENTS}/cmake_utilities/test_apps" \
       "${COMPONENTS}/cmake_utilities/docs"

# --- Guition board drivers -------------------------------------------------
say "Guition esp_lcd_jd9365"
mkdir -p "${COMPONENTS}/esp_lcd_jd9365/include"
for f in "${JD9365_FILES[@]}"; do
    curl -fSL --retry 3 --max-time 120 -o "${COMPONENTS}/esp_lcd_jd9365/${f}" \
        "${GUITION_RAW}/esp_lcd_jd9365/${f}"
done

say "Guition esp_lcd_touch_gsl3680"
mkdir -p "${COMPONENTS}/esp_lcd_touch_gsl3680/include"
for f in "${GSL3680_FILES[@]}"; do
    curl -fSL --retry 3 --max-time 120 -o "${COMPONENTS}/esp_lcd_touch_gsl3680/${f}" \
        "${GUITION_RAW}/esp_lcd_touch_gsl3680/${f}"
done

# --- the bit that makes the build offline ----------------------------------
say "removing component manifests so the component manager stays offline"
find "${COMPONENTS}" -name idf_component.yml -print -delete
find "${COMPONENTS}" -name '.component_hash' -delete

say "done"
du -sh "${COMPONENTS}"
echo
echo "Sanity check - this must print nothing:"
find "${REPO_ROOT}" -name idf_component.yml
