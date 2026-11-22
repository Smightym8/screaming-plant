#include "soil_monitor.h"

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_player.h"
#include "soil_moisture.h"

#define SOIL_TASK_STACK 3072
#define SOIL_TASK_PRIO  3

static const char* TAG = "soil_monitor";

static void soil_monitor_task(void* arg)
{
    const TickType_t cooldown = pdMS_TO_TICKS(CONFIG_SCREAM_COOLDOWN_S * 1000);
    TickType_t last_scream = 0;
    bool screamed = false;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        soil_moisture_reading_t r;
        esp_err_t err = soil_moisture_read(&r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reading the sensor failed: %s", esp_err_to_name(err));
        } else {
            bool dry = r.percent <= CONFIG_SOIL_MOISTURE_DRY_THRESHOLD_PERCENT;
            ESP_LOGI(TAG, "Soil moisture: %3d %%  (%4d mV, raw %4d)%s",
                     r.percent, r.voltage_mv, r.raw, dry ? "  -> dry" : "");

            TickType_t now = xTaskGetTickCount();
            if (dry && (!screamed || now - last_scream >= cooldown)) {
                /* log before queueing: the audio task has a higher priority and logs the file name at once */
                ESP_LOGW(TAG, "Too dry! Moisture %d %% (dry at <= %d %%), screaming:",
                         r.percent, CONFIG_SOIL_MOISTURE_DRY_THRESHOLD_PERCENT);
                if (audio_player_play_next() == ESP_OK) {
                    last_scream = now;
                    screamed = true;
                } else {
                    ESP_LOGW(TAG, "Audio player busy, trying again with the next reading");
                }
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_SOIL_MOISTURE_INTERVAL_MS));
    }
}

esp_err_t soil_monitor_start(void)
{
    BaseType_t ok = xTaskCreate(soil_monitor_task, "soil_monitor", SOIL_TASK_STACK, NULL, SOIL_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    ESP_LOGI(TAG, "Soil monitor started (every %d ms, dry at <= %d %%, cooldown %d s)",
             CONFIG_SOIL_MOISTURE_INTERVAL_MS, CONFIG_SOIL_MOISTURE_DRY_THRESHOLD_PERCENT,
             CONFIG_SCREAM_COOLDOWN_S);
    return ESP_OK;
}
