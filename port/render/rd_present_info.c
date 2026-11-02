/* rd_present_info.c: rd_last_present_info's record (rd.h RdPresentInfo):
 * what the last present drew, noted by rd_present (rd_interp.c) and by
 * rd_end_frame's present with interpolation off (rd__note_frame_present).
 *
 * The present notes it on the window's render thread and the game thread
 * reads it (the photo's line, the state picture's frame): a sequence lock,
 * the words copied one by one as atomics, so a read never sees half of a
 * note. */
#include <string.h>
#include "rd_internal.h"

#define INFO_WORDS (sizeof(RdPresentInfo) / sizeof(uint32_t))

_Static_assert(sizeof(RdPresentInfo) % sizeof(uint32_t) == 0, "RdPresentInfo is whole words");

/* the record as words, its sequence (odd while a note is written) and
 * whether a present ran */
static uint32_t s_words[INFO_WORDS];
static uint32_t s_seq;
static uint32_t s_valid;

static void infoWrite(const RdPresentInfo *in, uint32_t valid)
{
    uint32_t w[INFO_WORDS];
    memcpy(w, in, sizeof(w));
    const uint32_t seq = __atomic_load_n(&s_seq, __ATOMIC_RELAXED);
    __atomic_store_n(&s_seq, seq + 1u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (size_t i = 0; i < INFO_WORDS; i++) {
        __atomic_store_n(&s_words[i], w[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_valid, valid, __ATOMIC_RELAXED);
    __atomic_store_n(&s_seq, seq + 2u, __ATOMIC_RELEASE);
}

void rd__present_info_reset(void)
{
    RdPresentInfo z;
    memset(&z, 0, sizeof(z));
    infoWrite(&z, 0);
}

void rd__present_info_note(const RdFrame *cur, uint32_t firstOfTick, float alpha)
{
    RdPresentInfo i;
    i.frame = cur->number;
    i.keep = cur->keep;
    i.fade = cur->fade;
    i.snap = (uint32_t)rd__interp_snap(rd__prev_frame(), cur);
    i.firstOfTick = firstOfTick;
    i.alpha = alpha;
    infoWrite(&i, 1);
}

/* with interpolation off rd_end_frame presents each frame once, whole: the
 * tick's own picture */
void rd__note_frame_present(const RdFrame *f)
{
    rd__present_info_note(f, 1, 1.0f);
}

bool rd_last_present_info(RdPresentInfo *out)
{
    if (!out) {
        return false;
    }
    uint32_t w[INFO_WORDS], valid, s0, s1;
    do {
        s0 = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
        for (size_t i = 0; i < INFO_WORDS; i++) {
            w[i] = __atomic_load_n(&s_words[i], __ATOMIC_RELAXED);
        }
        valid = __atomic_load_n(&s_valid, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        s1 = __atomic_load_n(&s_seq, __ATOMIC_RELAXED);
    } while ((s0 & 1u) != 0 || s0 != s1);
    if (!valid) {
        return false;
    }
    memcpy(out, w, sizeof(w));
    return true;
}
