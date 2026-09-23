# Offline build

The requirement was that this project builds with no network access. It does,
and not by caching — by vendoring.

## How it works

1. **Every dependency lives in `components/`**, committed to the repository.
2. **There is no `idf_component.yml` anywhere in the tree.** They were deleted
   from every vendored component. With no manifests to resolve, the ESP-IDF
   component manager has nothing to fetch and never opens a socket.
3. The top-level `CMakeLists.txt` also sets `IDF_COMPONENT_MANAGER=0` before
   including `project.cmake`, as a second line of defence in case a manifest
   is reintroduced by accident.

There is no `dependencies.lock` and no `managed_components/` directory, and
`.gitignore` keeps both out.

## Verifying it really is offline

```bash
# should print nothing
find . -name idf_component.yml

# build with the network down, or:
IDF_COMPONENT_MANAGER=0 idf.py build
```

If a build ever tries to reach `components.espressif.com`, something has
reintroduced a manifest — that is the thing to look for.

## What is vendored, and at what version

| Directory | Version | Origin |
|---|---|---|
| `components/lvgl` | 9.2.2 | `lvgl/lvgl` on the ESP Component Registry |
| `components/esp_lvgl_port` | 2.4.4 | `espressif/esp_lvgl_port` |
| `components/esp_lcd_touch` | 1.1.2 | `espressif/esp_lcd_touch` |
| `components/esp_codec_dev` | 1.3.4 | `espressif/esp_codec_dev` |
| `components/cmake_utilities` | 0.5.3 | `espressif/cmake_utilities` |
| `components/esp_hosted` | 0.0.27 | `espressif/esp_hosted` (host side: `docs/`, `examples/`, `slave/` removed) |
| `components/esp_wifi_remote` | 0.4.1 | `espressif/esp_wifi_remote` |
| `components/esp_lcd_jd9365` | 1.0.2 | Guition vendor package (originally `espressif/esp_lcd_jd9365` from esp-iot-solution) |
| `components/esp_lcd_touch_gsl3680` | vendor | Guition vendor package |
| `components/bsp_jc8012p4a1` | — | written for this board |

`esp_lvgl_port` 2.4.4 was chosen over the newest 2.9.0 because 2.8.0+ require
ESP-IDF ≥ 5.2 features and were released well after v5.3; 2.4.4 declares
`idf >= 4.4`, supports LVGL 9, and includes `lvgl_port_add_disp_dsi()`.

## Pruning, and the one trap it creates

To keep the repository a sensible size, these were removed from the vendored
LVGL (136 MB → 18 MB):

`tests/`, `docs/`, `scripts/`, `zephyr/`, `demos/*`, `examples/*`

**`env_support/cmake/` was kept** — LVGL's root `CMakeLists.txt` includes
`env_support/cmake/esp.cmake`, so deleting it breaks the build outright.

**`demos/` and `examples/` still exist as empty directories**, each holding a
`.gitkeep` that explains why. `esp.cmake` passes both paths to
`idf_component_register(INCLUDE_DIRS ...)`, and ESP-IDF errors out on an
include directory that does not exist. The directories must be present; their
contents need not be.

Consequently `sdkconfig.defaults` pins:

```
CONFIG_LV_BUILD_EXAMPLES=n
CONFIG_LV_USE_DEMO_WIDGETS=n
CONFIG_LV_USE_DEMO_BENCHMARK=n
CONFIG_LV_USE_DEMO_MUSIC=n
CONFIG_LV_USE_DEMO_STRESS=n
```

Turning any of those on will fail at link time, because the sources they glob
for are not there. If you want the LVGL demos, re-extract the full LVGL
release over `components/lvgl`.

## Refreshing the vendored tree

On a machine with network access:

```bash
tools/vendor_components.sh          # bash / git-bash
tools\vendor_components.ps1         # PowerShell
```

The script downloads each pinned version from the ESP Component Registry and
the Guition vendor repository, extracts it into `components/`, deletes the
`idf_component.yml` files and re-applies the LVGL pruning. It does not touch
`components/bsp_jc8012p4a1`.

Edit the version table at the top of the script to move a pin.

## Toolchain

The toolchain itself is not vendored — ESP-IDF and the `riscv32-esp-elf`
compiler still have to be installed once. For a genuinely air-gapped setup,
mirror the IDF tools archives with:

```bash
python $IDF_PATH/tools/idf_tools.py download --all
```

on a networked machine, then copy `$IDF_TOOLS_PATH/dist` across and run
`idf_tools.py install` offline.

## Local patches

`tools/patches/*.patch` are applied by both vendor scripts after download.

- `esp_hosted-sdio-shared-host.patch`: the TF card (slot 0) and the ESP32-C6
  link (slot 1) share one SDMMC controller. On ESP-IDF v5.3 a second
  `sdmmc_host_init()` returns `ESP_ERR_INVALID_STATE`, which made ESP-Hosted
  give up whenever the card had been mounted first. The patch accepts it.

`esp_hosted` 0.0.27 and `esp_wifi_remote` 0.4.1 are the versions in the board
vendor's `esp_brookesia_phone` lock file, i.e. what the shipped C6 slave
firmware was built against. The host and slave RPC protocol has to match, so
do not bump `esp_hosted` without also reflashing the C6 with a matching slave.
`eppp_link` and `esp_serial_slave_link` are declared in the registry manifests
but not referenced by either component's CMake in the ESP-Hosted
configuration, so they are not vendored.
