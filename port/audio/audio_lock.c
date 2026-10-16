/*
 * port/audio/audio_lock.c
 *
 * The lock over the SPU2 and the driver (audio_lock.h).
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
