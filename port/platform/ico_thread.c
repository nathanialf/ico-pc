/* ico_thread.h */
#include "ico_thread.h"

#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

struct IcoThread {
    HANDLE h;
    void (*fn)(void *);
    void *arg;
};

struct IcoMutex {
    CRITICAL_SECTION cs;
};

struct IcoCond {
    CONDITION_VARIABLE cv;
};

static DWORD WINAPI thread_main(LPVOID p)
{
    IcoThread *t = (IcoThread *)p;

    t->fn(t->arg);
    return 0;
}

/* kernel32's CreateThread shares its name with the EE kernel call
   kernel_host.c defines; linking it by name pulls kernel32's import stub in
   beside that (a duplicate definition on x86-64, the v0.9.6-rc1 Windows
   link), so it is looked up at run time as diag_host.c does */
typedef HANDLE(WINAPI *Win32CreateThreadFn)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
                                            LPVOID, DWORD, LPDWORD);

static Win32CreateThreadFn win32_create_thread(void)
{
    static Win32CreateThreadFn fn;

    if (fn == NULL) {
        HMODULE k = GetModuleHandleA("kernel32.dll");

        fn = k != NULL ? (Win32CreateThreadFn)(void (*)(void))GetProcAddress(k, "CreateThread")
                       : NULL;
    }
    return fn;
}

IcoThread *ico_thread_create(void (*fn)(void *arg), void *arg)
{
    IcoThread *t = calloc(1, sizeof(*t));

    if (t == NULL) {
        return NULL;
    }
    t->fn = fn;
    t->arg = arg;
    {
        Win32CreateThreadFn create = win32_create_thread();

        t->h = create != NULL ? create(NULL, 0, thread_main, t, 0, NULL) : NULL;
    }
    if (t->h == NULL) {
        free(t);
        return NULL;
    }
    return t;
}

void ico_thread_join(IcoThread *t)
{
    if (t == NULL) {
        return;
    }
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}

IcoMutex *ico_mutex_create(void)
{
    IcoMutex *m = calloc(1, sizeof(*m));

    if (m != NULL) {
        InitializeCriticalSection(&m->cs);
    }
    return m;
}

void ico_mutex_destroy(IcoMutex *m)
{
    if (m != NULL) {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

void ico_mutex_lock(IcoMutex *m)
{
    EnterCriticalSection(&m->cs);
}

void ico_mutex_unlock(IcoMutex *m)
{
    LeaveCriticalSection(&m->cs);
}

IcoCond *ico_cond_create(void)
{
    IcoCond *c = calloc(1, sizeof(*c));

    if (c != NULL) {
        InitializeConditionVariable(&c->cv);
    }
    return c;
}

void ico_cond_destroy(IcoCond *c)
{
    free(c);
}

void ico_cond_wait(IcoCond *c, IcoMutex *m)
{
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
}

void ico_cond_broadcast(IcoCond *c)
{
    WakeAllConditionVariable(&c->cv);
}

double ico_thread_now_ms(void)
{
    LARGE_INTEGER f, n;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&n);
    return (double)n.QuadPart * 1000.0 / (double)f.QuadPart;
}

int ico_thread_cpu_count(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

#else

#define ICO_WANT_LIBC_SCHED 1 /* sched.h says why */

#include <pthread.h>

#undef ICO_WANT_LIBC_SCHED

#include <time.h>
#include <unistd.h>

struct IcoThread {
    pthread_t t;
    void (*fn)(void *);
    void *arg;
};

struct IcoMutex {
    pthread_mutex_t m;
};

struct IcoCond {
    pthread_cond_t c;
};

static void *thread_main(void *p)
{
    IcoThread *t = (IcoThread *)p;

    t->fn(t->arg);
    return NULL;
}

IcoThread *ico_thread_create(void (*fn)(void *arg), void *arg)
{
    IcoThread *t = calloc(1, sizeof(*t));

    if (t == NULL) {
        return NULL;
    }
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->t, NULL, thread_main, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void ico_thread_join(IcoThread *t)
{
    if (t == NULL) {
        return;
    }
    pthread_join(t->t, NULL);
    free(t);
}

IcoMutex *ico_mutex_create(void)
{
    IcoMutex *m = calloc(1, sizeof(*m));

    if (m != NULL && pthread_mutex_init(&m->m, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

void ico_mutex_destroy(IcoMutex *m)
{
    if (m != NULL) {
        pthread_mutex_destroy(&m->m);
        free(m);
    }
}

void ico_mutex_lock(IcoMutex *m)
{
    pthread_mutex_lock(&m->m);
}

void ico_mutex_unlock(IcoMutex *m)
{
    pthread_mutex_unlock(&m->m);
}

IcoCond *ico_cond_create(void)
{
    IcoCond *c = calloc(1, sizeof(*c));

    if (c != NULL && pthread_cond_init(&c->c, NULL) != 0) {
        free(c);
        return NULL;
    }
    return c;
}

void ico_cond_destroy(IcoCond *c)
{
    if (c != NULL) {
        pthread_cond_destroy(&c->c);
        free(c);
    }
}

void ico_cond_wait(IcoCond *c, IcoMutex *m)
{
    pthread_cond_wait(&c->c, &m->m);
}

void ico_cond_broadcast(IcoCond *c)
{
    pthread_cond_broadcast(&c->c);
}

double ico_thread_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int ico_thread_cpu_count(void)
{
    const long n = sysconf(_SC_NPROCESSORS_ONLN);

    return n > 0 ? (int)n : 1;
}

#endif
