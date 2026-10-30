/* ico_thread: the port's one small OS thread, mutex and condition wrapper
   for code that must not depend on SDL (the achievements file writer, the
   headless tests). pthreads on POSIX, the Win32 thread, critical section and
   condition variable on Windows. Not for the game's own threads (those are
   fibers, sched.h). */
#ifndef ICO_THREAD_H
#define ICO_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IcoThread IcoThread;
typedef struct IcoMutex IcoMutex;
typedef struct IcoCond IcoCond;

/* A thread running fn(arg); NULL when the OS could not make one. */
IcoThread *ico_thread_create(void (*fn)(void *arg), void *arg);
/* Waits for the thread to return, then frees it. */
void ico_thread_join(IcoThread *t);

IcoMutex *ico_mutex_create(void);
void ico_mutex_destroy(IcoMutex *m);
void ico_mutex_lock(IcoMutex *m);
void ico_mutex_unlock(IcoMutex *m);

IcoCond *ico_cond_create(void);
void ico_cond_destroy(IcoCond *c);
/* The mutex must be held; it is released while waiting and held again on return. */
void ico_cond_wait(IcoCond *c, IcoMutex *m);
void ico_cond_broadcast(IcoCond *c);

/* A monotonic clock in milliseconds (the origin is arbitrary). */
double ico_thread_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif
