#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount point of the SD card in the VFS, e.g. "/sdcard/foo.txt". */
#define SD_CARD_MOUNT_POINT "/sdcard"

/**
 * Initialise the SPI bus and mount the FAT filesystem of the SD card
 * (wiring: CS=IO7, MOSI=IO5, CLK=IO4, MISO=IO6).
 * The card is never formatted automatically.
 */
esp_err_t sd_card_mount(void);

/**
 * Read a whole file into buf and terminate it with '\0'.
 *
 * @param path     Full path, e.g. SD_CARD_MOUNT_POINT "/foo.txt"
 * @param buf      Destination buffer
 * @param buf_size Size of buf in bytes (incl. terminating '\0')
 * @param out_len  Optional: number of bytes read (without '\0')
 * @return ESP_OK, ESP_ERR_NOT_FOUND if the file can't be opened,
 *         ESP_ERR_INVALID_SIZE if the file did not fit completely into buf
 *         (buf then contains the truncated beginning), ESP_FAIL on read error.
 */
esp_err_t sd_card_read_file(const char* path, char* buf, size_t buf_size, size_t* out_len);

/** Unmount the filesystem and release the SPI bus. */
void sd_card_unmount(void);

#ifdef __cplusplus
}
#endif
