#include "sd_card.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

/*
 * Wiring as in the sdcard_demo README of "Embedded Systems mit RISC-V"
 * (github.com/klushund/embedded-systems-mit-risc-v). Note: MOSI/MISO are
 * swapped compared to docs/breadboard-plan.png.
 */
#define SD_PIN_CS   7
#define SD_PIN_MOSI 5
#define SD_PIN_CLK  4
#define SD_PIN_MISO 6

static const char* TAG = "sd";

static sdmmc_card_t* s_card;
static spi_host_device_t s_host_id = SPI2_HOST;

esp_err_t sd_card_mount(void)
{
    ESP_RETURN_ON_FALSE(!s_card, ESP_ERR_INVALID_STATE, TAG, "already mounted");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    s_host_id = host.slot;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(s_host_id, &bus_cfg, SDSPI_DEFAULT_DMA),
                        TAG, "SPI bus init failed");

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_PIN_CS;
    slot_cfg.host_id = s_host_id;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_CARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Mounting failed: no FAT filesystem found on the card");
        } else {
            ESP_LOGE(TAG, "Card init failed (%s) - check wiring and that a card is inserted",
                     esp_err_to_name(err));
        }
        s_card = NULL;
        spi_bus_free(s_host_id);
        return err;
    }

    ESP_LOGI(TAG, "SD card mounted at " SD_CARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sd_card_read_file(const char* path, char* buf, size_t buf_size, size_t* out_len)
{
    ESP_RETURN_ON_FALSE(path && buf && buf_size > 0, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(s_card, ESP_ERR_INVALID_STATE, TAG, "card not mounted");

    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = fread(buf, 1, buf_size - 1, f);
    buf[len] = '\0';

    esp_err_t err = ESP_OK;
    if (ferror(f)) {
        ESP_LOGE(TAG, "Error while reading %s", path);
        err = ESP_FAIL;
    } else if (fgetc(f) != EOF) {
        ESP_LOGW(TAG, "%s is larger than the buffer (%u bytes), content truncated",
                 path, (unsigned)buf_size);
        err = ESP_ERR_INVALID_SIZE;
    }
    fclose(f);

    if (out_len) {
        *out_len = len;
    }
    return err;
}

void sd_card_unmount(void)
{
    if (!s_card) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, s_card);
    s_card = NULL;
    spi_bus_free(s_host_id);
    ESP_LOGI(TAG, "SD card unmounted");
}
