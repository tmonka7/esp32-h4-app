<#
    Re-download every vendored dependency into components\.

    Run this on a machine WITH network access. The result is committed so the
    project itself builds offline. See docs\OFFLINE_BUILD.md.

    Usage:  .\tools\vendor_components.ps1
#>

$ErrorActionPreference = 'Stop'

$RepoRoot   = Split-Path -Parent $PSScriptRoot
$Components = Join-Path $RepoRoot 'components'
$Work       = Join-Path ([System.IO.Path]::GetTempPath()) ("vendor_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# --- pinned versions -------------------------------------------------------
$RegistryPins = @(
    @{ ns = 'lvgl';      name = 'lvgl';            ver = '9.2.2' },
    @{ ns = 'espressif'; name = 'esp_lvgl_port';   ver = '2.4.4' },
    @{ ns = 'espressif'; name = 'esp_lcd_touch';   ver = '1.1.2' },
    @{ ns = 'espressif'; name = 'esp_codec_dev';   ver = '1.3.4' },
    @{ ns = 'espressif'; name = 'cmake_utilities'; ver = '0.5.3' }
)

$GuitionRaw = 'https://raw.githubusercontent.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6/main/1-Demo/idf-examples/common_components'

$Jd9365Files = @(
    'CHANGELOG.md', 'CMakeLists.txt', 'README.md',
    'esp_lcd_jd9365.c', 'license.txt', 'include/esp_lcd_jd9365.h'
)
$Gsl3680Files = @(
    'CMakeLists.txt', 'esp_lcd_touch_gsl3680.c', 'gsl_point_id.c',
    'include/esp_lcd_touch_gsl3680.h', 'include/gsl_point_id.h'
)

# env_support MUST stay: lvgl\CMakeLists.txt includes env_support\cmake\esp.cmake
$LvglPrune = @('tests', 'docs', 'scripts', 'zephyr', 'CMakePresets.json', 'demos', 'examples')

function Say($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }

try {
    # --- registry components -------------------------------------------------
    foreach ($pin in $RegistryPins) {
        $url  = "https://components-file.espressif.com/components/$($pin.ns)/$($pin.name)/$($pin.ver)/$($pin.ns)__$($pin.name)-v$($pin.ver).zip"
        $zip  = Join-Path $Work "$($pin.ns)__$($pin.name).zip"
        $dest = Join-Path $Components $pin.name

        Say "$($pin.ns)/$($pin.name) @ $($pin.ver)"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300

        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Expand-Archive -Path $zip -DestinationPath $dest -Force
    }

    # --- LVGL pruning --------------------------------------------------------
    Say 'pruning components\lvgl'
    foreach ($d in $LvglPrune) {
        $p = Join-Path $Components "lvgl\$d"
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }

    # Of env_support only cmake\ is needed; the rest is other build systems.
    $envSupport = Join-Path $Components 'lvgl\env_support'
    if (Test-Path $envSupport) {
        Get-ChildItem -Path $envSupport -Force |
            Where-Object { $_.Name -ne 'cmake' } |
            ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
    }

    # esp.cmake passes these to idf_component_register(INCLUDE_DIRS ...) and
    # ESP-IDF errors on a missing include dir, so they must exist even empty.
    foreach ($d in @('demos', 'examples')) {
        $p = Join-Path $Components "lvgl\$d"
        New-Item -ItemType Directory -Force -Path $p | Out-Null
        $note = @"
LVGL bundled $d/ were removed from this vendored copy to keep the repo small.
This directory must exist because env_support/cmake/esp.cmake passes it to
idf_component_register(INCLUDE_DIRS ...), and ESP-IDF errors on a missing
include dir. Keep the matching CONFIG_LV_* options off (see sdkconfig.defaults).
"@
        Set-Content -Path (Join-Path $p '.gitkeep') -Value $note -Encoding utf8
    }

    # --- other trimming ------------------------------------------------------
    $trim = @(
        'esp_lvgl_port\test_apps', 'esp_lvgl_port\docs', 'esp_lvgl_port\examples',
        'esp_lvgl_port\images\lvgl8', 'esp_codec_dev\test_apps',
        'cmake_utilities\test_apps', 'cmake_utilities\docs'
    )
    foreach ($t in $trim) {
        $p = Join-Path $Components $t
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }

    # --- Guition board drivers ----------------------------------------------
    Say 'Guition esp_lcd_jd9365'
    New-Item -ItemType Directory -Force -Path (Join-Path $Components 'esp_lcd_jd9365\include') | Out-Null
    foreach ($f in $Jd9365Files) {
        $out = Join-Path $Components ("esp_lcd_jd9365\" + $f.Replace('/', '\'))
        Invoke-WebRequest -Uri "$GuitionRaw/esp_lcd_jd9365/$f" -OutFile $out -UseBasicParsing -TimeoutSec 120
    }

    Say 'Guition esp_lcd_touch_gsl3680'
    New-Item -ItemType Directory -Force -Path (Join-Path $Components 'esp_lcd_touch_gsl3680\include') | Out-Null
    foreach ($f in $Gsl3680Files) {
        $out = Join-Path $Components ("esp_lcd_touch_gsl3680\" + $f.Replace('/', '\'))
        Invoke-WebRequest -Uri "$GuitionRaw/esp_lcd_touch_gsl3680/$f" -OutFile $out -UseBasicParsing -TimeoutSec 120
    }

    # --- the bit that makes the build offline --------------------------------
    Say 'removing component manifests so the component manager stays offline'
    Get-ChildItem -Path $Components -Recurse -Force -Filter 'idf_component.yml' |
        ForEach-Object { Write-Host $_.FullName; Remove-Item -Force $_.FullName }
    Get-ChildItem -Path $Components -Recurse -Force -Filter '.component_hash' |
        ForEach-Object { Remove-Item -Force $_.FullName }

    Say 'done'
    Write-Host ''
    Write-Host 'Sanity check - this must list nothing:'
    Get-ChildItem -Path $RepoRoot -Recurse -Force -Filter 'idf_component.yml' |
        Select-Object -ExpandProperty FullName
}
finally {
    if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
}
