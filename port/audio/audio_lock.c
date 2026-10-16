/*
 * port/audio/audio_lock.c
 *
 * The lock over the SPU2 and the driver, and the driver's tick clock
 * (audio_lock.h).
 */
#include "audio_lock.h"

#include <stddef.h>

static IcoAudioLockFn lock_fn;
static IcoAudioLockFn unlock_fn;
static void *lock_user;

void ico_audio_lock(void)
{
    if (lock_fn != NULL) {
        lock_fn(lock_user);
    }
}

void ico_audio_unlock(void)
{
    if (unlock_fn != NULL) {
        unlock_fn(lock_user);
    }
}

void ico_audio_lock_install(IcoAudioLockFn lock, IcoAudioLockFn unlock, void *user)
{
    lock_fn = lock;
    unlock_fn = unlock;
    lock_user = user;
}

/* the driver's ticks: written by the serve and read by the take, both
   under the lock */
static IcoAudioTicks ticks;
static uint64_t tick_last_ns;

void ico_audio_tick_note(uint64_t now_ns)
{
    if (tick_last_ns != 0 && now_ns > tick_last_ns) {
        const uint64_t gap = now_ns - tick_last_ns;

        if (gap > ICO_AUDIO_TICK_LONG_NS) {
            ticks.long_gaps++;
        }
        if (gap > ticks.gap_max_ns) {
            ticks.gap_max_ns = gap;
        }
    }
    tick_last_ns = now_ns;
    ticks.ticks++;
}

void ico_audio_tick_take(IcoAudioTicks *out)
{
    ico_audio_lock();
    *out = ticks;
    ticks.gap_max_ns = 0;
    ico_audio_unlock();
}
