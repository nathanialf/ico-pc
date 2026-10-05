/*
 * port/audio/wav.c
 *
 * A RIFF/WAVE PCM writer for the audio dump (audio_host.h).  The sizes in
 * the header are patched every second of audio and at the close, so a run
 * that is killed still leaves a playable file.
 */
#include <stdio.h>
#include <stdlib.h>

#include "audio_host.h"

struct IcoWav {
    FILE *f;
    int rate;
    uint64_t frames;
    uint64_t since_patch;
};

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

static void header(IcoWav *w)
{
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0,  0, 0,   0,   'W', 'A', 'V', 'E', 'f', 'm', 't',
                     ' ', 16,  0,   0,   0,  1, 0,   2,   0,   0,   0,   0,   0,   0,   0,
                     0,   0,   4,   0,   16, 0, 'd', 'a', 't', 'a', 0,   0,   0,   0};
    uint64_t bytes = w->frames * 4;
    uint32_t data = bytes > 0xFFFFFFFFu - 36 ? 0xFFFFFFFFu - 36 : (uint32_t)bytes;
    long at = ftell(w->f);

    put32(h + 4, data + 36);
    put32(h + 24, (uint32_t)w->rate);
    put32(h + 28, (uint32_t)w->rate * 4);
    put32(h + 40, data);
    fseek(w->f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), w->f);
    if (at > 0) {
        fseek(w->f, at, SEEK_SET);
    }
    fflush(w->f);
}

IcoWav *ico_wav_open(const char *path, int rate)
{
    IcoWav *w = calloc(1, sizeof(*w));

    if (w == NULL) {
        return NULL;
    }
    w->f = fopen(path, "wb");
    if (w->f == NULL) {
        free(w);
        return NULL;
    }
    w->rate = rate;
    header(w);
    return w;
}

void ico_wav_write(IcoWav *w, const int16_t *frames, int count)
{
    uint8_t buf[4096];
    int i = 0;

    if (w == NULL || count <= 0) {
        return;
    }
    while (i < count) {
        int n = 0;

        /* little-endian on every host */
        while (i < count && n + 4 <= (int)sizeof(buf)) {
            put16(buf + n, (uint16_t)frames[2 * i]);
            put16(buf + n + 2, (uint16_t)frames[2 * i + 1]);
            n += 4;
            i++;
        }
        fwrite(buf, 1, (size_t)n, w->f);
    }
    w->frames += (uint64_t)count;
    w->since_patch += (uint64_t)count;
    if (w->since_patch >= (uint64_t)w->rate) {
        w->since_patch = 0;
        header(w);
    }
}

uint64_t ico_wav_close(IcoWav *w)
{
    uint64_t frames;

    if (w == NULL) {
        return 0;
    }
    header(w);
    fclose(w->f);
    frames = w->frames;
    free(w);
    return frames;
}
