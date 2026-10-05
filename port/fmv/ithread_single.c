/*
 * port/fmv/ithread_single.c
 *
 * libmpeg2's thread layer (common/ithread.h) for a decoder that never runs a
 * second thread.  The port creates the decoder with one core
 * (IMPEG2D_CMD_CTL_SET_NUM_CORES is never sent; the default is 1), so the
 * library only takes and releases mutexes and never creates a thread, waits
 * on a condition or a semaphore.  This file replaces the library's
 * pthread-based common/ithread.c, which is not compiled: it would pull
 * winpthreads into the Windows build for nothing, and the decoder runs on
 * the simulation thread only.
 *
 * Every function keeps the library's signature (ithread.h).  Mutexes and
 * conditions are no-ops; creating a thread fails, which the library treats
 * as an error on the multi-core path the port does not use.
 */
#include <string.h>
#include "iv_datatypedef.h"
#include "ithread.h"

UWORD32 ithread_get_handle_size(void)
{
    return sizeof(int);
}

UWORD32 ithread_get_mutex_lock_size(void)
{
    return sizeof(int);
}

WORD32 ithread_create(void *thread_handle, void *attribute, void *strt, void *argument)
{
    (void)thread_handle;
    (void)attribute;
    (void)strt;
    (void)argument;
    return -1;
}

WORD32 ithread_join(void *thread_handle, void **val_ptr)
{
    (void)thread_handle;
    (void)val_ptr;
    return 0;
}

void ithread_exit(void *val_ptr)
{
    (void)val_ptr;
}

WORD32 ithread_get_mutex_struct_size(void)
{
    return sizeof(int);
}

WORD32 ithread_mutex_init(void *mutex)
{
    memset(mutex, 0, sizeof(int));
    return 0;
}

WORD32 ithread_mutex_destroy(void *mutex)
{
    (void)mutex;
    return 0;
}

WORD32 ithread_mutex_lock(void *mutex)
{
    (void)mutex;
    return 0;
}

WORD32 ithread_mutex_unlock(void *mutex)
{
    (void)mutex;
    return 0;
}

void ithread_yield(void) {}

void ithread_sleep(UWORD32 u4_time)
{
    (void)u4_time;
}

void ithread_msleep(UWORD32 u4_time_ms)
{
    (void)u4_time_ms;
}

void ithread_usleep(UWORD32 u4_time_us)
{
    (void)u4_time_us;
}

UWORD32 ithread_get_sem_struct_size(void)
{
    return sizeof(int);
}

WORD32 ithread_sem_init(void *sem, WORD32 pshared, UWORD32 value)
{
    (void)sem;
    (void)pshared;
    (void)value;
    return 0;
}

WORD32 ithread_sem_post(void *sem)
{
    (void)sem;
    return 0;
}

WORD32 ithread_sem_wait(void *sem)
{
    (void)sem;
    return 0;
}

WORD32 ithread_sem_destroy(void *sem)
{
    (void)sem;
    return 0;
}

WORD32 ithread_set_affinity(WORD32 core_id)
{
    (void)core_id;
    return 1;
}

WORD32 ithread_get_cond_struct_size(void)
{
    return sizeof(int);
}

WORD32 ithread_cond_init(void *cond)
{
    (void)cond;
    return 0;
}

WORD32 ithread_cond_destroy(void *cond)
{
    (void)cond;
    return 0;
}

WORD32 ithread_cond_wait(void *cond, void *mutex)
{
    (void)cond;
    (void)mutex;
    return 0;
}

WORD32 ithread_cond_signal(void *cond)
{
    (void)cond;
    return 0;
}
