#include "wav.h"

#include <string.h>

#define WAV_FORMAT_PCM        1
#define WAV_FORMAT_EXTENSIBLE 0xFFFE

static uint16_t le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

wav_result_t wav_read_header(FILE* f, wav_info_t* info)
{
    uint8_t hdr[12];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        return WAV_ERR_IO;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return WAV_ERR_NOT_WAV;
    }

    int have_fmt = 0;
    memset(info, 0, sizeof(*info));

    /* Walk the chunks until "data"; skip everything else (LIST, fact, ...) */
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, sizeof(ch), f) != sizeof(ch)) {
            return WAV_ERR_IO;
        }
        uint32_t size = le32(ch + 4);

        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[40];
            if (size < 16) {
                return WAV_ERR_NOT_WAV;
            }
            size_t want = size < sizeof(fmt) ? size : sizeof(fmt);
            if (fread(fmt, 1, want, f) != want) {
                return WAV_ERR_IO;
            }
            uint16_t format = le16(fmt);
            if (format == WAV_FORMAT_EXTENSIBLE && want >= 26) {
                format = le16(fmt + 24); /* first two bytes of the sub-format GUID */
            }
            if (format != WAV_FORMAT_PCM) {
                return WAV_ERR_UNSUPPORTED;
            }
            info->channels = le16(fmt + 2);
            info->sample_rate = le32(fmt + 4);
            info->bits = le16(fmt + 14);
            if (fseek(f, (long)(size - want + (size & 1)), SEEK_CUR) != 0) {
                return WAV_ERR_IO;
            }
            have_fmt = 1;
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!have_fmt) {
                return WAV_ERR_NOT_WAV;
            }
            info->data_size = size;
            break;
        } else {
            /* chunks are padded to an even size */
            if (fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0) {
                return WAV_ERR_IO;
            }
        }
    }

    if ((info->channels != 1 && info->channels != 2) ||
        (info->bits != 8 && info->bits != 16) ||
        info->sample_rate < 4000 || info->sample_rate > 48000) {
        return WAV_ERR_UNSUPPORTED;
    }
    return WAV_OK;
}

size_t wav_frame_size(const wav_info_t* info)
{
    return (size_t)info->channels * (info->bits / 8);
}

size_t wav_convert_to_s16(const uint8_t* in, size_t in_bytes, const wav_info_t* info,
                          int16_t* out, int volume_percent)
{
    const size_t frame = wav_frame_size(info);
    const size_t frames = in_bytes / frame;

    for (size_t i = 0; i < frames; i++) {
        const uint8_t* p = in + i * frame;
        int32_t sum = 0;
        for (int c = 0; c < info->channels; c++) {
            if (info->bits == 8) {
                sum += ((int32_t)p[c] - 128) * 256;          /* 8 bit WAV is unsigned */
            } else {
                sum += (int16_t)le16(p + 2 * c);             /* 16 bit WAV is signed */
            }
        }
        int32_t s = sum / info->channels * volume_percent / 100;   /* stereo -> mono, volume */
        if (s > 32767) {
            s = 32767;
        } else if (s < -32768) {
            s = -32768;
        }
        out[i] = (int16_t)s;
    }
    return frames;
}
