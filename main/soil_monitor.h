#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the task that measures the soil moisture every CONFIG_SOIL_MOISTURE_INTERVAL_MS.
 * When the soil is dry (<= CONFIG_SOIL_MOISTURE_DRY_THRESHOLD_PERCENT) it asks the
 * audio player to scream, at most every CONFIG_SCREAM_COOLDOWN_S seconds.
 * soil_moisture_init() and audio_player_init() must have been called before.
 */
esp_err_t soil_monitor_start(void);

#ifdef __cplusplus
}
#endif
