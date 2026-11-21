/*
 * Host unit test for main/wav.c (runs on the PC, not on the ESP32).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -I main test/host/test_wav.c main/wav.c -o /tmp/test_wav && /tmp/test_wav
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "wav.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void put16(FILE* f, uint16_t v)
{
    fputc(v & 0xFF, f);
    fputc(v >> 8, f);
}

static void put32(FILE* f, uint32_t v)
{
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

/* Write a WAV file; optionally with an odd-sized LIST chunk before "data" and an extensible fmt chunk */
static FILE* make_wav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits,
                      const void* data, uint32_t data_size, int with_list)
{
    FILE* f = tmpfile();
    int extensible = format == 0xFFFE;
    uint32_t fmt_size = extensible ? 40 : 16;
    fwrite("RIFF", 1, 4, f);
    put32(f, 0); /* size is not checked by the parser */
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, fmt_size);
    put16(f, format);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * channels * bits / 8);
    put16(f, channels * bits / 8);
    put16(f, bits);
    if (extensible) {
        put16(f, 22);       /* cbSize */
        put16(f, bits);     /* valid bits */
        put32(f, 0);        /* channel mask */
        put16(f, 1);        /* sub-format GUID starts with PCM = 1 */
        for (int i = 0; i < 14; i++) {
            fputc(0, f);
        }
    }
    if (with_list) {
        fwrite("LIST", 1, 4, f);
        put32(f, 3);                  /* odd size -> one pad byte */
        fwrite("abc\0", 1, 4, f);
    }
    fwrite("data", 1, 4, f);
    put32(f, data_size);
    fwrite(data, 1, data_size, f);
    rewind(f);
    return f;
}

static void test_mono16(void)
{
    int16_t pcm[] = {0, 32767, -32768, 256, -256};
    FILE* f = make_wav(1, 1, 16000, 16, pcm, sizeof(pcm), 1);
    wav_info_t info;
    CHECK(wav_read_header(f, &info) == WAV_OK);
    CHECK(info.channels == 1 && info.sample_rate == 16000 && info.bits == 16);
    CHECK(info.data_size == sizeof(pcm));

    uint8_t raw[sizeof(pcm)];
    CHECK(fread(raw, 1, sizeof(raw), f) == sizeof(raw)); /* file is positioned at the samples */
    int16_t out[5];
    CHECK(wav_convert_to_s16(raw, sizeof(raw), &info, out, 100) == 5);   /* unchanged, bit exact */
    CHECK(out[0] == 0 && out[1] == 32767 && out[2] == -32768 && out[3] == 256 && out[4] == -256);

    CHECK(wav_convert_to_s16(raw, sizeof(raw), &info, out, 50) == 5);
    CHECK(out[1] == 16383 && out[2] == -16384 && out[3] == 128);

    CHECK(wav_convert_to_s16(raw, sizeof(raw), &info, out, 300) == 5);   /* louder: amplified + clipped */
    CHECK(out[1] == 32767 && out[2] == -32768 && out[3] == 768 && out[4] == -768);
    fclose(f);
}

static void test_stereo8(void)
{
    uint8_t pcm[] = {128, 128, 255, 255, 0, 0, 255, 1, 200}; /* last byte: incomplete frame */
    FILE* f = make_wav(1, 2, 8000, 8, pcm, sizeof(pcm), 0);
    wav_info_t info;
    CHECK(wav_read_header(f, &info) == WAV_OK);
    CHECK(wav_frame_size(&info) == 2);
    uint8_t raw[sizeof(pcm)];
    CHECK(fread(raw, 1, sizeof(raw), f) == sizeof(raw));
    int16_t out[4];
    CHECK(wav_convert_to_s16(raw, sizeof(raw), &info, out, 100) == 4);
    CHECK(out[0] == 0 && out[1] == 32512 && out[2] == -32768 && out[3] == 0);
    fclose(f);
}

static void test_extensible(void)
{
    int16_t pcm[] = {1000, -1000};
    FILE* f = make_wav(0xFFFE, 1, 22050, 16, pcm, sizeof(pcm), 0);
    wav_info_t info;
    CHECK(wav_read_header(f, &info) == WAV_OK);
    CHECK(info.sample_rate == 22050 && info.data_size == sizeof(pcm));
    fclose(f);
}

static void test_rejects(void)
{
    wav_info_t info;
    int16_t pcm[] = {0};

    FILE* f = make_wav(3, 1, 16000, 32, pcm, sizeof(pcm), 0);   /* IEEE float */
    CHECK(wav_read_header(f, &info) == WAV_ERR_UNSUPPORTED);
    fclose(f);

    f = make_wav(1, 1, 96000, 16, pcm, sizeof(pcm), 0);         /* sample rate too high */
    CHECK(wav_read_header(f, &info) == WAV_ERR_UNSUPPORTED);
    fclose(f);

    f = tmpfile();                                               /* e.g. an MP3 renamed to .wav */
    fwrite("ID3\x04\0\0\0\0\0\0\0\0\0\0", 1, 14, f);
    rewind(f);
    CHECK(wav_read_header(f, &info) == WAV_ERR_NOT_WAV);
    fclose(f);

    f = tmpfile();                                               /* truncated */
    fwrite("RIFF", 1, 4, f);
    rewind(f);
    CHECK(wav_read_header(f, &info) == WAV_ERR_IO);
    fclose(f);
}

int main(void)
{
    test_mono16();
    test_stereo8();
    test_extensible();
    test_rejects();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all wav tests passed\n");
    return 0;
}
