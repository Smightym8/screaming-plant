#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "soil_moisture.h"
#include "sd_card.h"

static const char* TAG = "main";

static void read_foo_txt(void)
{
    static char buf[512];
    size_t len = 0;
    esp_err_t err = sd_card_read_file(SD_CARD_MOUNT_POINT "/foo.txt", buf, sizeof(buf), &len);
    if (err == ESP_OK || err == ESP_ERR_INVALID_SIZE) {
        ESP_LOGI(TAG, "foo.txt (%u bytes):\n%s", (unsigned)len, buf);
    } else {
        ESP_LOGE(TAG, "Reading foo.txt failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    if (sd_card_mount() == ESP_OK) {
        read_foo_txt();
    }

    ESP_ERROR_CHECK(soil_moisture_init());

    while (true) {
        soil_moisture_reading_t r;
        esp_err_t err = soil_moisture_read(&r);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Soil moisture: %3d %%  (%4d mV, raw %4d)", r.percent, r.voltage_mv, r.raw);
        } else {
            ESP_LOGE(TAG, "Reading the sensor failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SOIL_MOISTURE_INTERVAL_MS));
    }
}
