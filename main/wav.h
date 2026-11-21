#pragma once

/*
 * Minimal WAV (RIFF) reader. Plain C without ESP-IDF dependencies, so it can be
 * unit-tested on the host (see test/host/test_wav.c).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t channels;      /**< 1 = mono, 2 = stereo */
    uint32_t sample_rate;   /**< Samples per second */
    uint16_t bits;          /**< 8 (unsigned) or 16 (signed) */
    uint32_t data_size;     /**< Size of the sample data in bytes */
} wav_info_t;

typedef enum {
    WAV_OK = 0,
    WAV_ERR_IO = -1,          /**< File too short / read error */
    WAV_ERR_NOT_WAV = -2,     /**< No RIFF/WAVE header */
    WAV_ERR_UNSUPPORTED = -3, /**< Not 8/16 bit PCM, mono/stereo, 4-48 kHz */
} wav_result_t;

/**
 * Parse the header and position the file at the start of the sample data.
 */
wav_result_t wav_read_header(FILE* f, wav_info_t* info);

/** Bytes per frame (one sample for every channel). */
size_t wav_frame_size(const wav_info_t* info);

/**
 * Convert raw PCM frames to signed 16 bit mono samples, scaled by volume.
 *
 * @param in             Raw sample data, in_bytes long (incomplete trailing frames are ignored)
 * @param out            Output buffer, must hold in_bytes / wav_frame_size() samples
 * @param volume_percent 0..400; 100 = unchanged, above 100 the signal is amplified and clipped
 * @return Number of samples written to out
 */
size_t wav_convert_to_s16(const uint8_t* in, size_t in_bytes, const wav_info_t* info,
                          int16_t* out, int volume_percent);

#ifdef __cplusplus
}
#endif
