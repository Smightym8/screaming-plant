#include "audio_player.h"

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "driver/i2s_pdm.h"
#include "sdkconfig.h"
#include "playlist.h"
#include "sd_card.h"
#include "wav.h"

/*
 * Playback pipeline:
 *   audio task --(16 bit PCM)--> I2S PDM TX in DAC line mode (DMA) --> IO3 --> RC low-pass --> AUX input of an active speaker
 *
 * The I2S peripheral turns 16 bit PCM into a high-rate 1-bit PDM stream in hardware (with noise shaping and
 * dithering). A passive RC low-pass filter turns that stream back into an analog line-level signal,
 * see docs/wiring.md. While nothing is playing the DMA sends zeros, i.e. a steady mid level, so the
 * output stays silent and does not pop between sounds.
 */
#define AUDIO_DOUT_GPIO     3
#define AUDIO_DEFAULT_RATE  48000
#define AUDIO_CHUNK_FRAMES  1024
#define AUDIO_FADE_MS       5          /* tiny fade in/out against clicks at the start/end of a file */
#define AUDIO_TASK_STACK    4096
#define AUDIO_TASK_PRIO     5

#define SIREN_MS            1500
#define SIREN_LOW_HZ        500
#define SIREN_HIGH_HZ       1500
#define SIREN_LEVEL         0.5f       /* -6 dBFS: loud enough, leaves headroom in the box */

static const char* TAG = "audio";

typedef enum {
    AUDIO_CMD_PLAY_NEXT,
} audio_cmd_t;

static QueueHandle_t s_queue;
static i2s_chan_handle_t s_tx;
static uint32_t s_rate;
static int16_t s_pcm[AUDIO_CHUNK_FRAMES];
static uint8_t s_raw[AUDIO_CHUNK_FRAMES * 4];   /* up to 16 bit stereo */
static int16_t s_sine[256];
static int s_count;   /* sounds in the current round */
static int s_pos;     /* sounds of the round already started */

static esp_err_t set_sample_rate(uint32_t rate)
{
    if (rate == s_rate) {
        return ESP_OK;
    }
    i2s_pdm_tx_clk_config_t clk = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(rate);
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "disable failed");
    esp_err_t err = i2s_channel_reconfig_pdm_tx_clock(s_tx, &clk);
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable failed");
    ESP_RETURN_ON_ERROR(err, TAG, "cannot switch to %u Hz", (unsigned)rate);
    s_rate = rate;
    return ESP_OK;
}

static esp_err_t write_pcm(const int16_t* pcm, size_t frames)
{
    size_t written = 0;
    return i2s_channel_write(s_tx, pcm, frames * sizeof(int16_t), &written, pdMS_TO_TICKS(1000));
}

/* Linear fade over the first and last `fade` frames of a sound of `total` frames */
static void apply_fade(int16_t* pcm, size_t n, uint32_t pos, uint32_t total, uint32_t fade)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t p = pos + i;
        uint32_t edge = p < total - p ? p : total - p;
        if (edge < fade) {
            pcm[i] = (int16_t)((int32_t)pcm[i] * (int32_t)edge / (int32_t)fade);
        }
    }
}

static esp_err_t play_wav(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    wav_info_t info;
    wav_result_t res = wav_read_header(f, &info);
    if (res != WAV_OK) {
        ESP_LOGE(TAG, "%s: %s", path, res == WAV_ERR_UNSUPPORTED ?
                 "unsupported format (need PCM 8/16 bit, mono/stereo, 4-48 kHz)" : "not a valid WAV file");
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const size_t frame = wav_frame_size(&info);
    const uint32_t total = info.data_size / frame;
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;
    ESP_LOGI(TAG, ">>> Now playing: %s  (sound %d/%d of this round, %u.%u s)", name, s_pos, s_count,
             (unsigned)(total / info.sample_rate), (unsigned)(total % info.sample_rate * 10 / info.sample_rate));
    ESP_LOGD(TAG, "%u Hz, %u bit, %s", (unsigned)info.sample_rate, info.bits, info.channels == 1 ? "mono" : "stereo");

    esp_err_t err = set_sample_rate(info.sample_rate);
    const uint32_t fade = info.sample_rate * AUDIO_FADE_MS / 1000;
    uint32_t pos = 0;
    while (err == ESP_OK && pos < total) {
        size_t want = total - pos < AUDIO_CHUNK_FRAMES ? total - pos : AUDIO_CHUNK_FRAMES;
        size_t got = fread(s_raw, frame, want, f);
        if (got == 0) {
            break;   /* file shorter than its header says */
        }
        size_t n = wav_convert_to_s16(s_raw, got * frame, &info, s_pcm, CONFIG_AUDIO_VOLUME_PERCENT);
        apply_fade(s_pcm, n, pos, total, fade);
        err = write_pcm(s_pcm, n);
        pos += n;
    }
    fclose(f);
    return err;
}

static esp_err_t play_siren(void)
{
    ESP_LOGI(TAG, ">>> Now playing: built-in siren (no WAV files on the SD card)");
    ESP_RETURN_ON_ERROR(set_sample_rate(AUDIO_DEFAULT_RATE), TAG, "rate");
    const uint32_t total = AUDIO_DEFAULT_RATE * SIREN_MS / 1000;
    const uint32_t fade = AUDIO_DEFAULT_RATE * AUDIO_FADE_MS / 1000;
    const uint32_t half = total / 2;
    uint32_t phase = 0;
    for (uint32_t pos = 0; pos < total;) {
        size_t n = 0;
        for (; n < AUDIO_CHUNK_FRAMES && pos < total; n++, pos++) {
            /* sine sweep up during the first half, down during the second half */
            uint32_t t = pos < half ? pos : total - pos;
            uint32_t freq = SIREN_LOW_HZ + (uint32_t)((uint64_t)(SIREN_HIGH_HZ - SIREN_LOW_HZ) * t / half);
            phase += (uint32_t)(((uint64_t)freq << 32) / AUDIO_DEFAULT_RATE);
            s_pcm[n] = s_sine[phase >> 24];
        }
        apply_fade(s_pcm, n, pos - n, total, fade);
        ESP_RETURN_ON_ERROR(write_pcm(s_pcm, n), TAG, "write failed");
    }
    return ESP_OK;
}

static bool is_wav_name(const char* name)
{
    /* skip hidden files, e.g. the "._name.wav" files macOS creates on FAT cards */
    if (name[0] == '.') {
        return false;
    }
    size_t len = strlen(name);
    return len > 4 && strcasecmp(name + len - 4, ".wav") == 0;
}

/* ---------- random numbers ----------
 * Without Wi-Fi/Bluetooth the hardware RNG of the ESP32-C3 only delivers pseudo-random numbers.
 * So once at start-up the SAR ADC noise is switched on as entropy source (the official ESP-IDF way,
 * bootloader_random_enable) to get a truly random seed for a small xorshift generator.
 * Must run before the ADC (soil sensor) is initialised.
 */
static uint32_t s_rng;

static void seed_rng(void)
{
    bootloader_random_enable();
    esp_fill_random(&s_rng, sizeof(s_rng));
    bootloader_random_disable();
    if (s_rng == 0) {
        s_rng = 0x9E3779B9u;   /* xorshift must not start at 0 */
    }
}

static uint32_t rng_next(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

/* ---------- shuffle playlist ----------
 * Every sound is played once per round in a "smart shuffle" order (like iTunes/Spotify): sounds of
 * the same person (file name without trailing digits, e.g. dom1..dom4) are spread over the round
 * instead of clustering. At the start of each round the card is scanned again (so added/removed
 * files are picked up) and reshuffled; the new round never starts with the last sound or person.
 */
static char** s_names;
static int* s_order;
static char s_last[256];

static void free_playlist(void)
{
    for (int i = 0; i < s_count; i++) {
        free(s_names[i]);
    }
    free(s_names);
    free(s_order);
    s_names = NULL;
    s_order = NULL;
    s_count = 0;
    s_pos = 0;
}

static void new_round(void)
{
    free_playlist();
    DIR* dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return;
    }
    int cap = 0;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_type == DT_DIR || !is_wav_name(e->d_name)) {
            continue;
        }
        if (s_count == cap) {
            cap = cap ? cap * 2 : 16;
            char** grown = realloc(s_names, (size_t)cap * sizeof(char*));
            if (!grown) {
                break;
            }
            s_names = grown;
        }
        s_names[s_count] = strdup(e->d_name);
        if (s_names[s_count]) {
            s_count++;
        }
    }
    closedir(dir);

    s_order = malloc((size_t)(s_count ? s_count : 1) * sizeof(int));
    if (!s_order) {
        free_playlist();
        return;
    }
    playlist_shuffle(s_order, s_count, (const char* const*)s_names, s_last, rng_next);
    s_pos = 0;
    ESP_LOGI(TAG, "New round: %d sound(s) on the card, shuffled", s_count);
    char line[160];
    size_t len = 0;
    for (int i = 0; i < s_count; i++) {   /* log the order, a few names per line */
        int w = snprintf(line + len, sizeof(line) - len, "%s%s", len ? ", " : "", s_names[s_order[i]]);
        if (w < 0 || (size_t)w >= sizeof(line) - len) {
            line[len] = '\0';   /* drop the name that did not fit, it starts the next line */
            ESP_LOGI(TAG, "  %s", line);
            len = 0;
            i--;
            continue;
        }
        len += (size_t)w;
    }
    if (len) {
        ESP_LOGI(TAG, "  %s", line);
    }
}

/* Next sound of the playlist; starts a new round when all sounds were played. */
static bool next_wav(char* path, size_t path_size)
{
    if (s_pos >= s_count) {
        new_round();
    }
    if (s_count == 0) {
        return false;
    }
    const char* name = s_names[s_order[s_pos]];
    s_pos++;
    snprintf(path, path_size, SD_CARD_MOUNT_POINT "/%s", name);
    strlcpy(s_last, name, sizeof(s_last));
    return true;
}

static esp_err_t play_next(void)
{
    static char path[300];
    /* skip files that vanished or cannot be played; give up after one full round */
    for (int attempt = 0; attempt <= s_count; attempt++) {
        if (!next_wav(path, sizeof(path))) {
            break;
        }
        esp_err_t err = play_wav(path);
        if (err == ESP_ERR_NOT_FOUND) {
            s_pos = s_count;   /* card changed: rescan with the next sound */
            continue;
        }
        if (err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
    }
    ESP_LOGW(TAG, "No playable WAV files in the root of the SD card");
    return play_siren();
}

static void audio_task(void* arg)
{
    audio_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        uint32_t start = esp_log_timestamp();
        esp_err_t err = play_next();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Playback finished after %u ms", (unsigned)(esp_log_timestamp() - start));
        } else {
            ESP_LOGE(TAG, "Playback failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t audio_player_init(void)
{
    seed_rng();
    for (int i = 0; i < 256; i++) {
        s_sine[i] = (int16_t)(32767.0f * SIREN_LEVEL * sinf(2.0f * (float)M_PI * (float)i / 256.0f));
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 480;              /* 10 ms per DMA buffer at 48 kHz */
    chan_cfg.auto_clear_after_cb = true;       /* send silence (mid level) whenever no new data is queued */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "I2S channel alloc failed");

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(AUDIO_DEFAULT_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_PCM_FMT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_GPIO_UNUSED,            /* DAC line mode: only the data line is needed */
            .dout = AUDIO_DOUT_GPIO,
            .dout2 = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_tx_mode(s_tx, &pdm_cfg), TAG, "PDM TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "I2S enable failed");
    s_rate = AUDIO_DEFAULT_RATE;

    s_queue = xQueueCreate(1, sizeof(audio_cmd_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue alloc failed");
    BaseType_t ok = xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    ESP_LOGI(TAG, "Audio player ready: I2S PDM (16 bit) on IO%d, volume %d %%",
             AUDIO_DOUT_GPIO, CONFIG_AUDIO_VOLUME_PERCENT);
    return ESP_OK;
}

esp_err_t audio_player_play_next(void)
{
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    audio_cmd_t cmd = AUDIO_CMD_PLAY_NEXT;
    return xQueueSend(s_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
