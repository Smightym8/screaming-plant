#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the audio task. Sound is output as 16 bit audio with the I2S PDM transmitter on IO3;
 * an RC low-pass filter turns it into a line-level signal for the AUX input of an active
 * speaker (see docs/wiring.md).
 *
 * Sounds are all *.wav files in the root directory of the SD card
 * (PCM, 8 or 16 bit, mono or stereo, 4-48 kHz). If there are none, a synthetic
 * siren is played instead.
 */
esp_err_t audio_player_init(void);

/**
 * Ask the audio task to play the next sound of the shuffled playlist: every file once per round,
 * sounds of the same person spread apart (smart shuffle), then a new order; the next round never
 * starts with the last sound or the same person.
 * Does not block.
 * @return ESP_OK if queued, ESP_ERR_INVALID_STATE if a request is already pending
 */
esp_err_t audio_player_play_next(void);

#ifdef __cplusplus
}
#endif
