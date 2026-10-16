/* Exercise the actual SDL queue without a device or an audio thread: the
   cushion (two vsyncs at first, doubled after a dry queue up to 120 ms,
   halved after 20 s without one), the refill and its 96-frame ramp down
   from the last pushed frame, the trim target, the drop limit and the
   queued-ms query (out_sdl.c's push, the sync path). */
#include "../out_sdl.c"

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int queued_frames(void)
{
    return SDL_GetAudioStreamQueued(stream) / BYTES_PER_FRAME;
}

/* the stream emptied and primed with the current cushion, as a resume */
static void reprime(void)
{
    SDL_ClearAudioStream(stream);
    buffer_silence(cushion_frames());
}

int main(void)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, CHANNELS, 48000};
    static int16_t pcm[800 * CHANNELS], received[(CUSHION_MAX_FRAMES + 800) * CHANNELS];
    int i;

    stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) {
        fprintf(stderr, "SDL stream: %s\n", SDL_GetError());
        return 1;
    }
    for (i = 0; i < 800 * CHANNELS; i++) {
        pcm[i] = (int16_t)(i + 1);
    }
    ico_audio_set_volume(1.0);
    CHECK(ico_audio_sdl_active());

    /* the first cushion: two vsyncs (800-frame blocks), primed */
    CHECK(cushion_frames() == 1600);
    CHECK(ico_audio_sdl_cushion_ms() == 33);
    reprime();
    CHECK(queued_frames() == 1600);
    CHECK(ico_audio_sdl_queued_ms() == 33);
    ico_audio_sdl_push(pcm, 800);
    CHECK(underruns == 0 && drops == 0);
    CHECK(queued_frames() == 2400);
    CHECK(ico_audio_sdl_queued_ms() == 50);

    /* a dry queue: the cushion doubles and silence fills to it before the
       block, which is kept whole; the silence starts as a ramp down from
       the last pushed frame (pcm's last, 1599 and 1600) over 96 frames */
    SDL_ClearAudioStream(stream);
    CHECK(ico_audio_sdl_queued_ms() == 0);
    ico_audio_sdl_push(pcm, 800);
    CHECK(underruns == 1 && drops == 0);
    CHECK(cushion_frames() == 3200);
    CHECK(queued_frames() == 3200 + 800);
    CHECK(SDL_GetAudioStreamData(stream, received, (3200 + 800) * BYTES_PER_FRAME) ==
          (3200 + 800) * BYTES_PER_FRAME);
    for (i = 0; i < 3200; i++) {
        const int l = i < RAMP_FRAMES ? 1599 * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES : 0;
        const int r = i < RAMP_FRAMES ? 1600 * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES : 0;

        CHECK(received[2 * i] == l && received[2 * i + 1] == r);
    }
    CHECK(received[2 * (RAMP_FRAMES - 1)] == 0 && received[0] == 1599 * 95 / 96);
    CHECK(memcmp(received + 3200 * CHANNELS, pcm, sizeof(pcm)) == 0);

    /* a resume primes the current cushion (the stream has no device, so
       the resume itself fails and logs a line) */
    ico_audio_sdl_pause(0);
    CHECK(queued_frames() == 3200);

    /* further dry queues: capped at 120 ms */
    SDL_ClearAudioStream(stream);
    ico_audio_sdl_push(pcm, 800);
    CHECK(underruns == 2 && cushion_frames() == CUSHION_MAX_FRAMES);
    CHECK(queued_frames() == CUSHION_MAX_FRAMES + 800);
    SDL_ClearAudioStream(stream);
    ico_audio_sdl_push(pcm, 800);
    CHECK(underruns == 3 && cushion_frames() == CUSHION_MAX_FRAMES);
    CHECK(ico_audio_sdl_cushion_ms() == 120);
    CHECK(cushion_peak == CUSHION_MAX_FRAMES);

    /* the trim follows the cushion: nothing above it, as is */
    reprime();
    ico_audio_sdl_push(pcm, 800);
    CHECK(applied_ratio == rate_ratio);

    /* 20 s without a dry queue (the queue held at the cushion): one step
       back, the surplus then trimmed; another 20 s: two vsyncs again */
    reprime();
    for (i = 0; i < 1200; i++) {
        ico_audio_sdl_push(pcm, 800);
        SDL_GetAudioStreamData(stream, received, 800 * BYTES_PER_FRAME);
    }
    CHECK(underruns == 3 && drops == 0);
    CHECK(cushion_frames() == 3200);
    ico_audio_sdl_push(pcm, 800);
    SDL_GetAudioStreamData(stream, received, 800 * BYTES_PER_FRAME);
    CHECK(applied_ratio > rate_ratio);
    for (i = 0; i < 1200; i++) {
        ico_audio_sdl_push(pcm, 800);
        SDL_GetAudioStreamData(stream, received, 800 * BYTES_PER_FRAME);
    }
    CHECK(cushion_frames() == 1600);
    for (i = 0; i < 1200; i++) {
        ico_audio_sdl_push(pcm, 800);
        SDL_GetAudioStreamData(stream, received, 800 * BYTES_PER_FRAME);
    }
    CHECK(cushion_frames() == 1600);

    /* the drop limit: the cushion plus four vsyncs.  A catch-up after a
       stall keeps five blocks over the cushion; the sixth is dropped */
    CHECK(queue_limit_frames() == 1600 + 3200);
    reprime();
    for (i = 0; i < 6; i++) {
        ico_audio_sdl_push(pcm, 800);
    }
    CHECK(drops == 1);
    CHECK(queued_frames() == 1600 + 5 * 800);
    /* at the largest cushion the same five, never past 250 ms */
    cushion_level = 2;
    CHECK(queue_limit_frames() == CUSHION_MAX_FRAMES + 3200);
    reprime();
    for (i = 0; i < 6; i++) {
        ico_audio_sdl_push(pcm, 800);
    }
    CHECK(drops == 2);
    CHECK(queued_frames() == CUSHION_MAX_FRAMES + 5 * 800);
    block_frames = 2000;
    CHECK(queue_limit_frames() == QUEUE_CEILING_FRAMES);
    block_frames = 800;

    /* an empty block: nothing */
    ico_audio_sdl_push(pcm, 0);
    CHECK(drops == 2);

    SDL_DestroyAudioStream(stream);
    stream = NULL;
    CHECK(!ico_audio_sdl_active());
    CHECK(ico_audio_sdl_queued_ms() == -1);
    CHECK(ico_audio_sdl_cushion_ms() == 0);
    return failures ? 1 : 0;
}
