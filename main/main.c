#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "soil_moisture.h"

static const char* TAG = "main";

void app_main(void)
{
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
