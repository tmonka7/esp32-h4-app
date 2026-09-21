/*
 * TF card on SDMMC slot 0.
 *
 * On the ESP32-P4 slot 0 is IOMUX-fixed: CLK GPIO43, CMD GPIO44, D0..D3
 * GPIO39..42. The card is powered from the on-chip LDO VO4 (see 4_CONN.png,
 * net ESP_LDO_VO4), so the host needs an sd_pwr_ctrl handle or the card never
 * comes out of reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static const char *TAG = "bsp_sd";

/* Unset Kconfig booleans are undefined rather than 0, so they cannot appear
 * directly in a struct initialiser. */
#ifdef CONFIG_BSP_SDCARD_FORMAT_IF_MOUNT_FAILED
#define SD_FORMAT_IF_MOUNT_FAILED 1
#else
#define SD_FORMAT_IF_MOUNT_FAILED 0
#endif

static sdmmc_card_t *s_card;
static sd_pwr_ctrl_handle_t s_pwr_ctrl;

bool bsp_sdcard_is_mounted(void)
{
    return s_card != NULL;
}

sdmmc_card_t *bsp_sdcard_get_card(void)
{
    return s_card;
}

esp_err_t bsp_sdcard_mount(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    if (s_pwr_ctrl == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = BSP_SD_PWR_LDO_CHAN,
        };
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl), TAG,
                            "cannot take LDO ch%d for the card supply", BSP_SD_PWR_LDO_CHAN);
    }
    host.pwr_ctrl_handle = s_pwr_ctrl;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk   = BSP_SD_CLK;
    slot_config.cmd   = BSP_SD_CMD;
    slot_config.d0    = BSP_SD_D0;
    slot_config.d1    = BSP_SD_D1;
    slot_config.d2    = BSP_SD_D2;
    slot_config.d3    = BSP_SD_D3;
    slot_config.width = 4;
    /* The board fits external 5k1 pull-ups (R47..R52), so the internal ones
     * are redundant; enabling them anyway is harmless and helps if a card
     * adapter is used. */
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = SD_FORMAT_IF_MOUNT_FAILED,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    const esp_err_t ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config,
                                                  &mount_config, &s_card);
    if (ret != ESP_OK) {
        s_card = NULL;
        if (ret == ESP_FAIL) {
            ESP_LOGW(TAG, "no filesystem on the card (mount %s failed)", BSP_SD_MOUNT_POINT);
        } else {
            ESP_LOGW(TAG, "card not mounted: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "TF card mounted at %s: %s, %llu MB",
             BSP_SD_MOUNT_POINT, s_card->cid.name,
             ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024ULL * 1024ULL));
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }
    const esp_err_t ret = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_card);
    s_card = NULL;
    return ret;
}

esp_err_t bsp_sdcard_usage(uint64_t *out_total_bytes, uint64_t *out_used_bytes)
{
    ESP_RETURN_ON_FALSE(s_card != NULL, ESP_ERR_INVALID_STATE, TAG, "no card mounted");

    uint64_t total = 0;
    uint64_t freeb = 0;
    ESP_RETURN_ON_ERROR(esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &freeb), TAG, "esp_vfs_fat_info");

    if (out_total_bytes) {
        *out_total_bytes = total;
    }
    if (out_used_bytes) {
        *out_used_bytes = (total > freeb) ? (total - freeb) : 0;
    }
    return ESP_OK;
}
