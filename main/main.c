#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_player.h"
#include "sd_card.h"
#include "soil_monitor.h"
#include "soil_moisture.h"

static const char* TAG = "main";

static void read_foo_txt(void)
{
    static char buf[512];
    size_t len = 0;
    esp_err_t err = sd_card_read_file(SD_CARD_MOUNT_POINT "/foo.txt", buf, sizeof(buf), &len);
    if (err == ESP_OK || err == ESP_ERR_INVALID_SIZE) {
        ESP_LOGI(TAG, "foo.txt (%u bytes):\n%s", (unsigned)len, buf);
    } else {
        ESP_LOGW(TAG, "Reading foo.txt failed: %s", esp_err_to_name(err));
    }
}

/*
 * Task overview:
 *   soil_monitor task: measures the soil moisture periodically, requests a scream when dry
 *   audio task:        plays a random WAV from the SD card (or a built-in siren) on request
 * app_main only initialises the hardware and starts the tasks, then returns.
 */
void app_main(void)
{
    if (sd_card_mount() == ESP_OK) {
        read_foo_txt();
    } else {
        ESP_LOGW(TAG, "No SD card, the plant can only use the built-in siren");
    }

    ESP_ERROR_CHECK(audio_player_init());
    ESP_ERROR_CHECK(soil_moisture_init());
    ESP_ERROR_CHECK(soil_monitor_start());
}
