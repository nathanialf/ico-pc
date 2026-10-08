/*
 * port/platform/diag_host.c
 *
 * Crash handlers, heartbeat, watchdog and milestone lines (diag_host.h).
 *
 * Every line is formatted into a static buffer and written with one OS call
 * to a handle of its own on the log file (opened for appending, as the
 * redirected stdout and stderr are), so nothing waits in a stdio buffer and
 * the crash and watchdog paths take no C library stream lock.
 *
 * The heartbeat and the watchdog run on their own OS thread: the game's
 * threads are fibers on the main thread, and one that spins without a
 * kernel call would stop anything run from the host loop. The thread reads
 * the scheduler's plain fields (sched.h, ico_sched_view) without locking;
 * a value may be a moment stale, which is fine for a report.
 *
 * A crash on Windows is reported from that thread too (the faulting thread
 * signals it and waits), so a stack overflow does not have to format the
 * report on the stack that overflowed. Before ico_diag_start, and on POSIX
 * (an alternate signal stack), the faulting thread reports itself.
 *
 * Android: the game is libmain.so, so addresses are given from its load
 * address; the lines also go to logcat; after a crash's block the handler
 * puts the system's handlers back and returns, so the fault repeats into
 * them and the system still writes its tombstone.
 */
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#include "diag_host.h"
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fiber.h"
#include "host_fs.h"
#include "sched.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#else

#include <fcntl.h>

#define ICO_WANT_LIBC_SCHED 1 /* sched.h says why */

#include <pthread.h>

#undef ICO_WANT_LIBC_SCHED

#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef __ANDROID__

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>

#endif

#endif
#define ICO_ENTRY
#define LINE_MAX_BYTES 1024
#define HEARTBEAT_S 2.0
#define NO_PROGRESS_BEATS 3
/* the watchdog polls every 0.25 s; a longer gap between two polls means
   the system or the process was suspended (watchdog_loop) */
#define SUSPEND_GAP_S 5.0
/* with an effects program loaded, the part of one present above
   PRESENT_FREE_NS is excused from the watchdog, for at most PRESENT_EXCUSE_NS */
#define PRESENT_FREE_NS (1000000000ull)
#define PRESENT_EXCUSE_NS (300ull * 1000000000ull)
#define STACK_SCAN_BYTES (64 * 1024)
#define STACK_HITS 24
#define FUNC_NAMES 64
#define THREAD_LINES_AFTER_BOOT 16
#define EXIT_CRASH 3
#define EXIT_WATCHDOG 4

/* --- State ----------------------------------------------------------------- */

static char log_path_copy[1024];

static int inited;

static IcoDiagStatusFn status_fn;

static unsigned int (*ticks_fn)(void);
static unsigned int (*vsyncs_fn)(void);
/* ico_diag_note_progress and ico_diag_set_movie: written by the game's
   thread, read by the watchdog's; a plain aligned word, as the tick count */
static volatile unsigned int progress;

static volatile int movie_playing;
/* ico_diag_present_enter/leave (diag_host.h): when the main thread entered
   the present it is in (0 = not inside one), and the capped time of the
   presents that finished, both in ico_diag_now_ns */
static volatile unsigned long long present_since_ns;

static volatile unsigned long long present_done_ns;
/* ico_diag_set_effects_program: written by the host before the first present */
static volatile int effects_program;
static void (*exit_hook)(const char *reason);

#ifndef _WIN32
/* v0.4.2 (Android): ico_diag_set_fatal_ui's box and flush, and the
   requests the crashing thread leaves for the watchdog thread (fatal_ui_*) */
static void (*fatal_box)(const char *text);
static void (*fatal_flush)(void);
static volatile sig_atomic_t ui_flush_req, ui_flush_done, ui_box_req, ui_box_done;
static char ui_box_text[1536];
#endif

static char failure[512];

static char thread_names[ICO_SCHED_MAX_THREADS][32];

static void *thread_funcs[ICO_SCHED_MAX_THREADS];

static struct {
    void *fn;
    const char *name;
} func_names[FUNC_NAMES];

static int n_func_names;

static int kanban_seen;

static int kanban_boot = -1000;

static int kanban_mc = -1000;

/* the module the game is in */
static uintptr_t exe_lo;

static uintptr_t exe_hi;

static char exe_name[64] = "ico_pc";

/* one crash (or watchdog report) at a time */
static volatile int fatal_once;

/* the fault the report describes */
static struct {
    const char *what;   /* "EXCEPTION_ACCESS_VIOLATION", "SIGSEGV", "abort()" */
    unsigned long code; /* the exception code or signal number */
    uintptr_t pc;       /* 0 when unknown (abort) */
    uintptr_t sp;
    uintptr_t fault_addr;
    int fault_kind; /* 0 read, 1 write, 8 execute, -1 none */
    int thread;     /* the scheduler's current thread then */
    uintptr_t stack_lo, stack_hi;
} crash;

/* --- Platform: the log, time, locks ------------------------------------------ */

#ifdef _WIN32

/* kernel32's CreateThread, SuspendThread and ResumeThread share their names
   with the EE kernel calls kernel_host.c defines; linking them by name would
   pull kernel32's import stubs in beside those (a duplicate definition on
   x86-64), so they are looked up at run time. */
typedef HANDLE(WINAPI *CreateThreadFn)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
                                       LPVOID, DWORD, LPDWORD);
typedef DWORD(WINAPI *SuspendResumeFn)(HANDLE);
static CreateThreadFn w32_create_thread;
static SuspendResumeFn w32_suspend_thread;
static SuspendResumeFn w32_resume_thread;

static void w32_threads(void)
{
    HMODULE k = GetModuleHandleA("kernel32.dll");
    if (k == NULL) {
        return;
    }
    w32_create_thread = (CreateThreadFn)(void (*)(void))GetProcAddress(k, "CreateThread");
    w32_suspend_thread = (SuspendResumeFn)(void (*)(void))GetProcAddress(k, "SuspendThread");
    w32_resume_thread = (SuspendResumeFn)(void (*)(void))GetProcAddress(k, "ResumeThread");
}

static HANDLE log_h = INVALID_HANDLE_VALUE;

static CRITICAL_SECTION lock;

static ULONGLONG t0;

static HANDLE main_thread;

static DWORD main_tid;

static HANDLE crash_event;

static HANDLE crash_done;

static DWORD reporter_tid;

static int reporter_running;

static void raw_write(const char *s, size_t n)
{
    DWORD done;
    HANDLE h = log_h != INVALID_HANDLE_VALUE ? log_h : GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE && h != NULL) {
        WriteFile(h, s, (DWORD)n, &done, NULL);
    }
}

double ico_diag_uptime(void)
{
    return (double)(GetTickCount64() - t0) / 1000.0;
}

unsigned long long ico_diag_now_ns(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&c);
    return (unsigned long long)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
}

static void lock_take(void)
{
    EnterCriticalSection(&lock);
}

static void lock_give(void)
{
    LeaveCriticalSection(&lock);
}

/* Package Q1: stdout and stderr are fully buffered on Windows
   (host_config.c ico_host_redirect_output; msvcrt writes an unbuffered
   stream one character per OS call). Before a fatal report their content
   is written out by a helper thread, waited on for at most STDIO_FLUSH_MS:
   the stopped thread may hold a stream's lock, and a flush that faults
   only stops the helper (crash_from_exception parks a second faulting
   thread). */
#define STDIO_FLUSH_MS 500

ICO_ENTRY static DWORD WINAPI stdio_flush_main(LPVOID arg)
{
    (void)arg;
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static void flush_stdio_bounded(void)
{
    DWORD tid;
    HANDLE h = w32_create_thread != NULL
                   ? w32_create_thread(NULL, 64 * 1024, stdio_flush_main, NULL, 0, &tid)
                   : NULL;

    if (h != NULL) {
        WaitForSingleObject(h, STDIO_FLUSH_MS);
        CloseHandle(h);
    }
}

/* a milestone on the main thread: what stderr holds goes first */
static void flush_stdio_if_main(void)
{
    if (GetCurrentThreadId() == main_tid) {
        fflush(stdout);
        fflush(stderr);
    }
}

#else

static int log_fd = -1;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct timespec t0;

static pthread_t main_thread;

static uintptr_t main_stack_lo, main_stack_hi;

static int reporter_running;

#ifdef __ANDROID__
/* the watchdog thread (ico_diag_start): the crash handler hands it the
   player's box */
static pthread_t reporter_thread;
#endif

static void raw_write(const char *s, size_t n)
{
    int fd = log_fd >= 0 ? log_fd : 2;
#ifdef __ANDROID__
    /* logcat too: written to the file, these lines bypass stderr and its
       mirror (one line, its newline dropped) */
    if (log_fd >= 0 && n > 0 && n < LINE_MAX_BYTES) {
        char line[LINE_MAX_BYTES];

        memcpy(line, s, n);
        line[s[n - 1] == '\n' ? n - 1 : n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "ico-pc", line);
    }
#endif
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w <= 0) {
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

double ico_diag_uptime(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0.tv_sec) + (double)(t.tv_nsec - t0.tv_nsec) / 1e9;
}

unsigned long long ico_diag_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

static void lock_take(void)
{
    pthread_mutex_lock(&lock);
}

static void lock_give(void)
{
    pthread_mutex_unlock(&lock);
}

/* POSIX keeps stdout and stderr unbuffered (glibc formats a whole printf
   call before its one write): nothing waits in them */
static void flush_stdio_bounded(void) {}

static void flush_stdio_if_main(void) {}

#endif

/* Formats one line (adding the newline) into buf and writes it. */
static void vline(char *buf, size_t size, const char *fmt, va_list ap)
{
    int n = vsnprintf(buf, size - 1, fmt, ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > size - 2) {
        n = (int)size - 2;
    }
    buf[n++] = '\n';
    raw_write(buf, (size_t)n);
}

void ico_diag_log(const char *fmt, ...)
{
    static char buf[LINE_MAX_BYTES];
    va_list ap;
    if (inited) {
        lock_take();
    }
    va_start(ap, fmt);
    vline(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (inited) {
        lock_give();
    }
}

/* The fatal paths' writer: no lock (the lock's holder may be the thread
   that crashed or the main thread the watchdog interrupted). */
static void flog(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);

static void flog(const char *fmt, ...)
{
    static char buf[LINE_MAX_BYTES];
    va_list ap;
    va_start(ap, fmt);
    vline(buf, sizeof buf, fmt, ap);
    va_end(ap);
}

const char *ico_diag_log_path(void)
{
    return log_path_copy[0] != '\0' ? log_path_copy : "(the console)";
}

void ico_diag_set_failure(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(failure, sizeof failure, fmt, ap);
    va_end(ap);
}

void ico_diag_set_sources(IcoDiagStatusFn status, unsigned int (*main_ticks)(void),
                          unsigned int (*vsyncs)(void))
{
    status_fn = status;
    ticks_fn = main_ticks;
    vsyncs_fn = vsyncs;
}

void ico_diag_note_progress(void)
{
    progress++;
}

unsigned int ico_diag_progress(void)
{
    return progress;
}

void ico_diag_set_movie(int playing)
{
    movie_playing = playing;
}

void ico_diag_set_exit_hook(void (*fn)(const char *reason))
{
    exit_hook = fn;
}

/* --- Addresses and names ----------------------------------------------------- */

static int in_exe(uintptr_t a)
{
    return a >= exe_lo && a < exe_hi;
}

/* "ico_pc.exe+0x1234", "msvcrt.dll+0x56" or "0x...". */
static void describe(uintptr_t a, char *out, size_t size)
{
    if (a == 0) {
        snprintf(out, size, "(unknown)");
        return;
    }
    if (in_exe(a)) {
        snprintf(out, size, "%s+0x%lx", exe_name, (unsigned long)(a - exe_lo));
        return;
    }
#ifdef _WIN32
    {
        HMODULE m = NULL;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)a, &m) &&
            GetModuleFileNameA(m, path, sizeof path) > 0) {
            const char *b = strrchr(path, '\\');
            snprintf(out, size, "%s+0x%lx", b != NULL ? b + 1 : path,
                     (unsigned long)(a - (uintptr_t)m));
            return;
        }
    }
#endif
    snprintf(out, size, "0x%llx", (unsigned long long)a);
}

static const char *func_name(void *fn)
{
    int i;
    for (i = 0; i < n_func_names; i++) {
        if (func_names[i].fn == fn) {
            return func_names[i].name;
        }
    }
    return NULL;
}

void ico_diag_name_func(void *func, const char *name)
{
    int i;
    for (i = 0; i < n_func_names; i++) {
        if (func_names[i].fn == func) {
            func_names[i].name = name;
            return;
        }
    }
    if (n_func_names < FUNC_NAMES) {
        func_names[n_func_names].fn = func;
        func_names[n_func_names].name = name;
        n_func_names++;
    }
}

/* The thread's name: given, else its function's, else its function's
   address. */
static const char *thread_name(int id, char *tmp, size_t size)
{
    IcoSchedView v;
    void *fn;
    const char *n;
    if (id == 0) {
        return "host loop";
    }
    if (id < 0 || id >= ICO_SCHED_MAX_THREADS) {
        return "?";
    }
    if (thread_names[id][0] != '\0') {
        return thread_names[id];
    }
    fn = thread_funcs[id];
    if (fn == NULL && ico_sched_view(id, &v)) {
        fn = v.entry;
    }
    n = fn != NULL ? func_name(fn) : NULL;
    if (n != NULL) {
        return n;
    }
    if (fn == NULL) {
        return "?";
    }
    {
        char d[64];
        describe((uintptr_t)fn, d, sizeof d);
        snprintf(tmp, size, "fn %s", d);
    }
    return tmp;
}

/* --- Thread lines -------------------------------------------------------------- */

static const char *state_name(int s)
{
    switch (s) {
    case ICO_THS_RUN:
        return "RUN";
    case ICO_THS_READY:
        return "READY";
    case ICO_THS_WAIT:
        return "WAIT";
    case ICO_THS_SUSPEND:
        return "SUSPEND";
    case ICO_THS_WAITSUSPEND:
        return "WAITSUSPEND";
    case ICO_THS_DORMANT:
        return "DORMANT";
    }
    return "?";
}

/* One thread's line; returns 0 when id is not a thread. */
static int thread_line(int id, char *out, size_t size)
{
    IcoSchedView v;
    char tmp[80];
    char wait[96];
    char last[160];
    char at[64];
    if (!ico_sched_view(id, &v)) {
        return 0;
    }
    wait[0] = '\0';
    if (v.status == ICO_THS_WAIT || v.status == ICO_THS_WAITSUSPEND) {
        if (v.wait_type == ICO_TSW_SLEEP) {
            snprintf(wait, sizeof wait, " sleeping (SleepThread)");
        } else if (v.wait_type == ICO_TSW_SEMA) {
            int count = 0;
            int waiters = 0;
            ico_sched_sema_view(v.wait_id, &count, &waiters);
            snprintf(wait, sizeof wait, " on sema %d (count %d, %d waiting)", v.wait_id, count,
                     waiters);
        }
    } else if (v.spinning) {
        snprintf(wait, sizeof wait, " busy-waiting for vsync %u", v.spin_until);
    }
    if (v.note != NULL) {
        describe((uintptr_t)v.note_caller, at, sizeof at);
        snprintf(last, sizeof last, "; last call %s(%d) from %s at vsync %u", v.note, v.note_arg,
                 at, v.note_vsync);
    } else {
        last[0] = '\0';
    }
    snprintf(out, size, "%c#%-3d %-22s pri %3d (init %3d) %-7s%s%s%s",
             id == ico_sched_current() ? '*' : ' ', id, thread_name(id, tmp, sizeof tmp),
             v.priority, v.init_priority, state_name(v.status), wait,
             v.wakeup_count > 0 ? " (wakeup pending)" : "", last);
    return 1;
}

static void dump_threads(void)
{
    static char line[LINE_MAX_BYTES];
    int id;
    flog("ico_pc: threads (* = on the CPU; priority 0 is the highest):");
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (thread_line(id, line, sizeof line)) {
            flog("ico_pc:   %s", line);
        }
    }
}

/* --- Process memory and the fatal end's player box (v0.4.2) --------------------- */

void ico_diag_set_fatal_ui(void (*box)(const char *text), void (*flush)(void))
{
#ifdef _WIN32
    /* Windows has its own box (finish) */
    (void)box;
    (void)flush;
#else
    fatal_box = box;
    fatal_flush = flush;
#endif
}

#ifndef _WIN32

/* "Name:   1234 kB" in a /proc status text: the number, or -1 */
static long status_kb(const char *text, const char *name)
{
    const char *p = text;
    const size_t n = strlen(name);
    while (*p != '\0') {
        if (strncmp(p, name, n) == 0) {
            long v = 0;
            p += n;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p < '0' || *p > '9') {
                return -1;
            }
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p++ - '0');
            }
            return v;
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return -1;
}

#endif

int ico_diag_process_memory(long *rss_kb, long *peak_kb)
{
    *rss_kb = *peak_kb = -1;
#ifdef _WIN32
    return -1;
#else
    /* open and read only: callable from the crash handler */
    char buf[4096];
    ssize_t got = 0;
    const int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    while (got < (ssize_t)sizeof buf - 1) {
        const ssize_t r = read(fd, buf + got, sizeof buf - 1 - (size_t)got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    close(fd);
    buf[got] = '\0';
    *rss_kb = status_kb(buf, "VmRSS:");
    *peak_kb = status_kb(buf, "VmHWM:");
    return *rss_kb >= 0 ? 0 : -1;
#endif
}

/* " | memory N MB (most M MB)", or "" where unknown */
static void memory_note(char *out, size_t size)
{
    long rss, peak;
    out[0] = '\0';
    if (ico_diag_process_memory(&rss, &peak) == 0) {
        snprintf(out, size, " | memory %ld MB (most %ld MB)", rss / 1024,
                 (peak >= 0 ? peak : rss) / 1024);
    }
}

#ifdef __ANDROID__

static int on_reporter(void)
{
    return reporter_running && pthread_equal(pthread_self(), reporter_thread);
}

/* the crashing thread: waits up to ms for the watchdog thread to set *flag */
static int ui_wait(volatile sig_atomic_t *flag, unsigned int ms)
{
    struct timespec ts = {0, 20 * 1000 * 1000};
    unsigned int t;
    for (t = 0; t < ms && !*flag; t += 20) {
        nanosleep(&ts, NULL);
    }
    return *flag != 0;
}

/* What the process wrote to stderr just before the end (the C library's
   abort message) into the log, ahead of the report: on the watchdog thread
   at once, from another thread through it, waiting at most a second. */
static void fatal_ui_flush(void)
{
    if (fatal_flush == NULL) {
        return;
    }
    if (on_reporter()) {
        fatal_flush();
    } else if (reporter_running) {
        ui_flush_done = 0;
        ui_flush_req = 1;
        ui_wait(&ui_flush_done, 1000);
    }
}

/* The player's box: on the watchdog thread itself, else through it, the
   crashing thread waiting until it is dismissed (at most two minutes) */
static void fatal_ui_box(const char *text)
{
    size_t n;
    if (fatal_box == NULL) {
        return;
    }
    n = strlen(text);
    if (n >= sizeof ui_box_text) {
        n = sizeof ui_box_text - 1;
    }
    memcpy(ui_box_text, text, n);
    ui_box_text[n] = '\0';
    if (on_reporter()) {
        fatal_box(ui_box_text);
    } else if (reporter_running) {
        ui_box_done = 0;
        ui_box_req = 1;
        ui_wait(&ui_box_done, 120000);
    }
}

#endif

#ifndef _WIN32

/* the watchdog thread's side, at each poll */
static void fatal_ui_serve(void)
{
    if (ui_flush_req && !ui_flush_done) {
        if (fatal_flush != NULL) {
            fatal_flush();
        }
        ui_flush_done = 1;
    }
    if (ui_box_req && !ui_box_done) {
        if (fatal_box != NULL) {
            fatal_box(ui_box_text);
        }
        ui_box_done = 1;
    }
}

#endif

#ifdef __ANDROID__

/* the box's words: what happened, the saves, and the log to send */
static void fatal_text(char *out, size_t size, const char *what)
{
    snprintf(out, size,
             "%s\n\nYour saves are kept. To report it, send the file %s with a few words about "
             "what you did just before.",
             what, log_path_copy[0] != '\0' ? log_path_copy : "logs/ico-pc.log");
}

#endif

/* --- Heartbeat ------------------------------------------------------------------ */

/* The heartbeat's content without the time; 1 when it differs from the
   last one. */
static void heartbeat_body(char *out, size_t size)
{
    char st[512];
    char tmp[80];
    int id;
    int ready = 0;
    int waiting = 0;
    int cur = ico_sched_current();
    IcoSchedView v;
    st[0] = '\0';
    if (status_fn != NULL) {
        status_fn(st, sizeof st);
    }
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (ico_sched_view(id, &v)) {
            if (v.status == ICO_THS_READY || v.status == ICO_THS_RUN) {
                ready++;
            } else if (v.status == ICO_THS_WAIT || v.status == ICO_THS_WAITSUSPEND) {
                waiting++;
            }
        }
    }
    if (cur != 0 && ico_sched_view(cur, &v)) {
        snprintf(out, size, "%s | on CPU #%d %s pri %d | ready %d, waiting %d | switches %lu", st,
                 cur, thread_name(cur, tmp, sizeof tmp), v.priority, ready, waiting,
                 ico_sched_switch_count());
    } else {
        snprintf(out, size, "%s | on CPU: host loop | ready %d, waiting %d | switches %lu", st,
                 ready, waiting, ico_sched_switch_count());
    }
    if (kanban_seen) {
        size_t n = strlen(out);
        snprintf(out + n, size - n, " | kanbanBoot step %d, card step %d", kanban_boot, kanban_mc);
    }
    {
        /* the second sign of life (ico_diag_note_progress): a movie's
           pictures move it while the tick stands still */
        size_t n = strlen(out);
        snprintf(out + n, size - n, " | progress %u%s", progress,
                 movie_playing ? " (a movie is playing)" : "");
    }
}

static char last_body[1024];

static unsigned int same_beats;

static void heartbeat(int fatal_path)
{
    static char body[1024];
    static char mem[64];
    heartbeat_body(body, sizeof body);
    if (strcmp(body, last_body) == 0) {
        same_beats++;
    } else {
        same_beats = 0;
        memcpy(last_body, body, sizeof body);
    }
    /* v0.4.2: the process's memory (a phone ends a process that holds too
       much without a word), outside the comparison: it moves while the game
       stands still */
    memory_note(mem, sizeof mem);
    if (fatal_path) {
        flog("ico_pc: heartbeat %.1f s | %s%s", ico_diag_uptime(), body, mem);
    } else if (same_beats + 1 >= NO_PROGRESS_BEATS) {
        ico_diag_log("ico_pc: heartbeat %.1f s | %s%s | no progress for %.0f s", ico_diag_uptime(),
                     body, mem, (double)same_beats * HEARTBEAT_S);
    } else {
        ico_diag_log("ico_pc: heartbeat %.1f s | %s%s", ico_diag_uptime(), body, mem);
    }
}

/* --- Milestones and the game's hooks --------------------------------------------- */

static void prefix(char *out, size_t size)
{
    char tmp[80];
    int cur = ico_sched_current();
    snprintf(out, size, "[%.3f s, vsync %u, tick %u, #%d %s]", ico_diag_uptime(),
             vsyncs_fn != NULL ? vsyncs_fn() : 0u, ticks_fn != NULL ? ticks_fn() : 0u, cur,
             thread_name(cur, tmp, sizeof tmp));
}

void ico_diag_milestone(const char *fmt, ...)
{
    char p[160];
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    prefix(p, sizeof p);
    flush_stdio_if_main();
    ico_diag_log("ico_pc: %s %s", p, msg);
}

void ico_host_milestone(const char *what)
{
    ico_diag_milestone("%s", what);
}

void ico_host_name_func(void *func, const char *name)
{
    ico_diag_name_func(func, name);
}

void ico_host_thread_func(int id, void *func, int priority)
{
    char tmp[80];
    char tmp2[80];
    IcoSchedView v;
    if (id <= 0 || id >= ICO_SCHED_MAX_THREADS) {
        return;
    }
    static unsigned int logged;
    thread_funcs[id] = func;
    thread_names[id][0] = '\0';
    if (!inited) {
        return;
    }
    /* every one until the first Main tick, then a few (the game makes
       short-lived threads all the time) */
    if (ticks_fn != NULL && ticks_fn() > 0 && logged >= THREAD_LINES_AFTER_BOOT) {
        return;
    }
    if (ticks_fn != NULL && ticks_fn() > 0 && ++logged == THREAD_LINES_AFTER_BOOT) {
        ico_diag_milestone("(further thread creations are not logged)");
    }
    ico_sched_view(id, &v);
    ico_diag_milestone("thread #%d created by #%d %s: priority %d, runs %s", id, v.parent,
                       thread_name(v.parent, tmp2, sizeof tmp2), priority,
                       thread_name(id, tmp, sizeof tmp));
}

void ico_host_kanban_step(int boot_step, int mc_check_step)
{
    if (boot_step == kanban_boot && mc_check_step == kanban_mc) {
        return;
    }
    kanban_seen = 1;
    kanban_boot = boot_step;
    kanban_mc = mc_check_step;
    if (inited) {
        ico_diag_milestone("kanbanBoot: bootStep %d, mcCheckStep %d", boot_step, mc_check_step);
    }
}

/* --- Stacks ------------------------------------------------------------------------ */

/* Words in [lo, hi) that point into the executable: return-address
   candidates, innermost first. */
static void scan_stack(const unsigned char *lo, const unsigned char *hi, uintptr_t shown_base,
                       uintptr_t real_base)
{
    char line[LINE_MAX_BYTES];
    size_t n = 0;
    int hits = 0;
    const unsigned char *p;
    line[0] = '\0';
    (void)shown_base;
    (void)real_base;
    for (p = lo; p + sizeof(uintptr_t) <= hi && hits < STACK_HITS; p += sizeof(uintptr_t)) {
        uintptr_t w;
        memcpy(&w, p, sizeof w);
        if (in_exe(w)) {
            n +=
                (size_t)snprintf(line + n, sizeof line - n, " +0x%lx", (unsigned long)(w - exe_lo));
            hits++;
            if (n > sizeof line - 32) {
                break;
            }
        }
    }
    flog("ico_pc: stack, return-address candidates in %s (innermost first):%s", exe_name,
         hits > 0 ? line : " none found");
}

#ifdef _WIN32

/* [sp, end of its committed region), at most STACK_SCAN_BYTES */
static void stack_bounds(uintptr_t sp, uintptr_t *lo, uintptr_t *hi)
{
    MEMORY_BASIC_INFORMATION mbi;
    *lo = *hi = 0;
    if (sp == 0 || VirtualQuery((LPCVOID)sp, &mbi, sizeof mbi) == 0 || mbi.State != MEM_COMMIT) {
        return;
    }
    *lo = sp;
    *hi = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if (*hi - *lo > STACK_SCAN_BYTES) {
        *hi = *lo + STACK_SCAN_BYTES;
    }
}

#else

/* the running fiber's stack, else the main thread's (called on the thread
   whose stack it is) */
static void stack_bounds_here(uintptr_t sp, uintptr_t *lo, uintptr_t *hi)
{
    void *a;
    void *b;
    *lo = *hi = 0;
    if (ico_fiber_current_stack(&a, &b) == 0 && sp >= (uintptr_t)a && sp < (uintptr_t)b) {
        *lo = sp;
        *hi = (uintptr_t)b;
    } else if (sp >= main_stack_lo && sp < main_stack_hi) {
        *lo = sp;
        *hi = main_stack_hi;
    }
    if (*hi - *lo > STACK_SCAN_BYTES) {
        *hi = *lo + STACK_SCAN_BYTES;
    }
}

#endif

/* --- The fatal end ------------------------------------------------------------------- */

static void finish(const char *reason, const char *box_text, int code)
{
    flog("ico_pc: the run ended: %s", reason);
    if (exit_hook != NULL) {
        exit_hook(reason);
    }
#ifdef _WIN32
    {
        char box[1536];
        snprintf(box, sizeof box,
                 "%s\n\nPlease report it at github.com/nathanialf/ico-pc/issues and attach "
                 "the logs folder from beside the program. The log is:\n%s",
                 box_text, ico_diag_log_path());
        MessageBoxA(NULL, box, "ICO PC", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    if (log_h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(log_h);
    }
    TerminateProcess(GetCurrentProcess(), (UINT)code);
#else
#ifdef __ANDROID__
    {
        /* v0.4.2: the watchdog's end (a crash keeps the system's handlers,
           crash_handler): the box, then the end */
        static char box[1536];
        fatal_ui_flush();
        fatal_text(box, sizeof box, box_text);
        fatal_ui_box(box);
    }
#else
    (void)box_text;
#endif
    _exit(code);
#endif
}

/* the crash block in the log, without the end */
static void report_crash_block(void)
{
    char where[96];
    char tmp[80];
    char line[LINE_MAX_BYTES];
    flush_stdio_bounded();
    describe(crash.pc, where, sizeof where);
    flog("ico_pc: ======================================================================");
    flog("ico_pc: CRASH: %s (0x%lx) at %s", crash.what, crash.code, where);
    if (crash.fault_kind >= 0) {
        flog("ico_pc: the faulting access: %s address 0x%llx",
             crash.fault_kind == 1   ? "writing"
             : crash.fault_kind == 8 ? "executing"
                                     : "reading",
             (unsigned long long)crash.fault_addr);
    }
#ifdef __ANDROID__
    flog("ico_pc: %s's offsets are relative to its load address; ico_pc.map (or "
         "llvm-symbolizer on the unstripped %s) gives the symbol (map address = offset)",
         exe_name, exe_name);
#else
    flog("ico_pc: the executable's offsets are relative to its load address; ico_pc.map gives "
         "the symbol (map address = __image_base__ + offset)");
#endif
    flog("ico_pc: game thread then: #%d %s", crash.thread,
         thread_name(crash.thread, tmp, sizeof tmp));
    if (crash.thread != 0 && thread_line(crash.thread, line, sizeof line)) {
        flog("ico_pc:   %s", line);
    }
    if (failure[0] != '\0') {
        flog("ico_pc: last failure message: %s", failure);
    }
    if (crash.stack_hi > crash.stack_lo) {
        scan_stack((const unsigned char *)crash.stack_lo, (const unsigned char *)crash.stack_hi, 0,
                   0);
    }
    heartbeat(1);
    dump_threads();
}

#ifndef __ANDROID__

static void report_crash(void)
{
    char box[256];

    report_crash_block();
    /* the player's box says what to do; what and where are in the log */
    snprintf(box, sizeof box, "ICO PC hit an error and had to close.");
    finish(crash.what, box, EXIT_CRASH);
}

#endif

/* --- Watchdog ------------------------------------------------------------------------ */

static unsigned char sample_stack[32 * 1024];

/* 1 from ico_diag_init until ico_diag_main_thread_end: the thread that
   called ico_diag_init (the one the watchdog samples) still runs */
static volatile int main_alive;

static size_t sample_len;

static uintptr_t sample_pc;

static uintptr_t sample_sp;

static int sample_thread;

#ifdef _WIN32

static int sample_main(void)
{
    CONTEXT c;
    uintptr_t lo;
    uintptr_t hi;
    sample_len = 0;
    if (!main_alive || w32_suspend_thread == NULL || w32_resume_thread == NULL ||
        w32_suspend_thread(main_thread) == (DWORD)-1) {
        return -1;
    }
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(main_thread, &c)) {
#if defined(_M_X64) || defined(__x86_64__)
        sample_pc = (uintptr_t)c.Rip;
        sample_sp = (uintptr_t)c.Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
        sample_pc = (uintptr_t)c.Pc;
        sample_sp = (uintptr_t)c.Sp;
#endif
        stack_bounds(sample_sp, &lo, &hi);
        if (hi > lo) {
            sample_len = hi - lo > sizeof sample_stack ? sizeof sample_stack : hi - lo;
            memcpy(sample_stack, (const void *)lo, sample_len);
        }
    }
    sample_thread = ico_sched_current();
    w32_resume_thread(main_thread);
    return 0;
}

#else

static volatile sig_atomic_t sample_done;

/* main_alive (ico_diag_main_thread_end) is cleared under this lock and
   read under it around the signal, so a thread that has ended is never
   signalled */
static pthread_mutex_t main_alive_lock = PTHREAD_MUTEX_INITIALIZER;

ICO_ENTRY static void sample_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    uintptr_t lo;
    uintptr_t hi;
    (void)sig;
    (void)si;
#if defined(__x86_64__)
    sample_pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    sample_sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
    sample_pc = (uintptr_t)uc->uc_mcontext.pc;
    sample_sp = (uintptr_t)uc->uc_mcontext.sp;
#else
    (void)uc;
#endif
    sample_len = 0;
    stack_bounds_here(sample_sp, &lo, &hi);
    if (hi > lo) {
        sample_len = hi - lo > sizeof sample_stack ? sizeof sample_stack : hi - lo;
        memcpy(sample_stack, (const void *)lo, sample_len);
    }
    sample_thread = ico_sched_current();
    sample_done = 1;
}

static int sample_main(void)
{
    struct timespec ts = {0, 10 * 1000 * 1000};
    int i;
    int sent;
    sample_done = 0;
    sample_len = 0;
    pthread_mutex_lock(&main_alive_lock);
    sent = main_alive && pthread_kill(main_thread, SIGUSR2) == 0;
    pthread_mutex_unlock(&main_alive_lock);
    if (!sent) {
        return -1;
    }
    for (i = 0; i < 50 && !sample_done; i++) {
        nanosleep(&ts, NULL);
    }
    return sample_done ? 0 : -1;
}

#endif

void ico_diag_set_effects_program(int loaded)
{
    effects_program = loaded != 0;
}

/* The excused part of a present that has lasted d ns: nothing without an
   effects program; with one, what is above PRESENT_FREE_NS, capped. */
static unsigned long long present_excused(unsigned long long d)
{
    if (!effects_program || d <= PRESENT_FREE_NS) {
        return 0;
    }
    d -= PRESENT_FREE_NS;
    return d < PRESENT_EXCUSE_NS ? d : PRESENT_EXCUSE_NS;
}

void ico_diag_present_enter(void)
{
    present_since_ns = ico_diag_now_ns();
}

void ico_diag_present_leave(void)
{
    const unsigned long long since = present_since_ns;
    if (since != 0) {
        const unsigned long long d = ico_diag_now_ns() - since;
        present_done_ns += present_excused(d);
    }
    present_since_ns = 0;
}

/* Seconds of present time excused so far (finished ones plus the one in
   progress, see present_excused). *current is the one in progress, 0 if none. */
static double present_seconds(double *current)
{
    unsigned long long done, since, now, cur = 0;
    do {
        done = present_done_ns;
        since = present_since_ns;
        now = ico_diag_now_ns();
    } while (done != present_done_ns);
    if (since != 0 && now > since) {
        cur = now - since;
    }
    if (current != NULL) {
        *current = (double)cur / 1e9;
    }
    return (double)(done + present_excused(cur)) / 1e9;
}

static void watchdog_fire(const char *reason)
{
    char where[96];
    char tmp[80];
    char box[512];
    if (fatal_once++) {
        return;
    }
    flush_stdio_bounded();
    flog("ico_pc: ======================================================================");
    flog("ico_pc: WATCHDOG: %s", reason);
    {
        double cur;
        (void)present_seconds(&cur);
        if (cur > 0.0) {
            flog("ico_pc: the game has waited %.0f s for a picture to be shown (an effects "
                 "program may be preparing its effects)",
                 cur);
        }
    }
    if (sample_main() == 0) {
        describe(sample_pc, where, sizeof where);
        flog("ico_pc: the main thread was at %s, running #%d %s", where, sample_thread,
             thread_name(sample_thread, tmp, sizeof tmp));
        flog("ico_pc: (a game thread that never reaches a kernel call keeps the host loop from "
             "running; the address shows where)");
        if (sample_len > 0) {
            scan_stack(sample_stack, sample_stack + sample_len, 0, 0);
        }
    } else {
        flog("ico_pc: could not sample the main thread");
    }
    if (failure[0] != '\0') {
        flog("ico_pc: last failure message: %s", failure);
    }
    heartbeat(1);
    dump_threads();
    /* the reason is in the log; the box says what happened in plain words */
#ifdef __ANDROID__
    snprintf(box, sizeof box, "ICO stopped responding, so it has to close.");
#else
    snprintf(box, sizeof box, "ICO PC stopped responding, so it was closed.");
#endif
    finish("watchdog", box, EXIT_WATCHDOG);
}

static unsigned int wd_first_s;

static unsigned int wd_later_s;

/* ico_diag_watchdog_pause: set by any thread, read by the watchdog's */
static volatile int wd_paused;

void ico_diag_watchdog_pause(int paused)
{
    if ((paused != 0) == (wd_paused != 0)) {
        return;
    }
    wd_paused = paused != 0;
    ico_diag_log("ico_pc: diagnostics: watchdog %s", paused ? "paused" : "running again");
}

void ico_diag_main_thread_end(void)
{
#ifndef _WIN32
    pthread_mutex_lock(&main_alive_lock);
#endif
    main_alive = 0;
#ifndef _WIN32
    pthread_mutex_unlock(&main_alive_lock);
#endif
}

static void watchdog_loop(void)
{
    double start = ico_diag_uptime();
    double last_beat = start;
    double last_alive = start;
    double last_poll = start;
    /* seconds inside presents when the clock last restarted: those since
       are not the game's idle time */
    const double pres_start = present_seconds(NULL);
    double pres_alive = pres_start;
    unsigned int last_ticks = 0;
    unsigned int last_progress = progress;
    char reason[160];
    for (;;) {
        double now;
        unsigned int ticks;
        unsigned int prog;
        double pres;
#ifdef _WIN32
        if (WaitForSingleObject(crash_event, 250) == WAIT_OBJECT_0) {
            report_crash();
            SetEvent(crash_done);
            Sleep(INFINITE);
        }
#else
        struct timespec ts = {0, 250 * 1000 * 1000};
        nanosleep(&ts, NULL);
        /* v0.4.2: a crashing thread's flush and box (fatal_ui_*) */
        fatal_ui_serve();
#endif
        if (fatal_once || !main_alive) {
            continue;
        }
        now = ico_diag_uptime();
        if (now - last_poll > SUSPEND_GAP_S || wd_paused) {
            /* the clock jumped between two polls: the system slept (Windows'
               GetTickCount64 counts sleep and hibernation) or the process was
               stopped (a debugger, SIGSTOP); or the watchdog is paused (the
               app in the background). That time is not the game's, so
               neither limit counts it. */
            start += now - last_poll;
            last_alive += now - last_poll;
        }
        last_poll = now;
        if (wd_paused) {
            last_beat = now;
            continue;
        }
        if (now - last_beat >= HEARTBEAT_S) {
            last_beat = now;
            heartbeat(0);
        }
        /* alive while either moves: a Main tick, or the progress a long
           stretch inside one Main tick reports (a movie plays whole inside
           one: the 137 s attract outlasted the later limit) */
        ticks = ticks_fn != NULL ? ticks_fn() : 0;
        prog = progress;
        pres = present_seconds(NULL);
        if (ticks != last_ticks || prog != last_progress) {
            last_ticks = ticks;
            last_progress = prog;
            last_alive = now;
            pres_alive = pres;
        }
        if (wd_first_s != 0 && ticks == 0 && now - start - (pres - pres_start) >= wd_first_s) {
            snprintf(reason, sizeof reason, "no Main tick %u s after boot started (watchdog=%u)",
                     wd_first_s, wd_first_s);
            watchdog_fire(reason);
        } else if (wd_later_s != 0 && ticks > 0 &&
                   now - last_alive - (pres - pres_alive) >= wd_later_s) {
            if (movie_playing) {
                snprintf(reason, sizeof reason,
                         "a movie was playing and showed no new picture for %u s (the last "
                         "was tick %u, progress %u)",
                         wd_later_s, ticks, prog);
            } else {
                snprintf(reason, sizeof reason,
                         "no new Main tick for %u s (the last was tick %u, progress %u)",
                         wd_later_s, ticks, prog);
            }
            watchdog_fire(reason);
        }
    }
}

#ifdef _WIN32

ICO_ENTRY static DWORD WINAPI watchdog_main(LPVOID arg)
{
    (void)arg;
    watchdog_loop();
    return 0;
}

#else

ICO_ENTRY static void *watchdog_main(void *arg)
{
    (void)arg;
    watchdog_loop();
    return NULL;
}

#endif

void ico_diag_start(unsigned int first_s, unsigned int later_s)
{
    wd_first_s = first_s;
    wd_later_s = later_s;
    if (!inited || reporter_running) {
        return;
    }
#ifdef _WIN32
    {
        HANDLE h = w32_create_thread != NULL
                       ? w32_create_thread(NULL, 256 * 1024, watchdog_main, NULL, 0, &reporter_tid)
                       : NULL;
        if (h == NULL) {
            ico_diag_log("ico_pc: diagnostics: cannot start the watchdog thread");
            return;
        }
        CloseHandle(h);
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, watchdog_main, NULL) != 0) {
            ico_diag_log("ico_pc: diagnostics: cannot start the watchdog thread");
            return;
        }
#ifdef __ANDROID__
        reporter_thread = t;
#endif
        pthread_detach(t);
    }
#endif
    reporter_running = 1;
    ico_diag_log("ico_pc: diagnostics: heartbeat every %.0f s; watchdog: first Main tick within "
                 "%u s, then a new one or a movie picture within %u s (0 = off)",
                 HEARTBEAT_S, first_s, later_s);
}

/* --- Crash handlers ------------------------------------------------------------------- */

#ifdef _WIN32

static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION (a trap: __builtin_trap / ICO_BREAK)";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT:
        return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_UNDERFLOW:
        return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_STACK_CHECK:
        return "EXCEPTION_FLT_STACK_CHECK";
    case 0xC00002B5:
        return "STATUS_FLOAT_MULTIPLE_TRAPS";
    case 0xC00002B4:
        return "STATUS_FLOAT_MULTIPLE_FAULTS";
    case 0xC0000409:
        return "STATUS_STACK_BUFFER_OVERRUN (fast fail)";
    }
    return "an exception";
}

static int fatal_code(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_STACK_CHECK:
    case 0xC00002B5:
    case 0xC00002B4:
        return 1;
    }
    return 0;
}

/* The faulting thread: record, hand the report to the watchdog thread (a
   fresh stack) and wait, or report here when there is none. */
static void crash_common(void)
{
    if (reporter_running && GetCurrentThreadId() != reporter_tid) {
        SetEvent(crash_event);
        WaitForSingleObject(crash_done, 60000);
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    report_crash();
}

static void crash_from_exception(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (InterlockedExchange((volatile LONG *)&fatal_once, 1) != 0) {
        /* a second fault while reporting: stop at once */
        if (GetCurrentThreadId() != reporter_tid) {
            Sleep(60000);
        }
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    crash.what = exception_name(r->ExceptionCode);
    crash.code = r->ExceptionCode;
    crash.pc = (uintptr_t)r->ExceptionAddress;
#if defined(_M_X64) || defined(__x86_64__)
    crash.sp = (uintptr_t)c->Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    crash.sp = (uintptr_t)c->Sp;
#endif
    crash.fault_kind = -1;
    if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r->NumberParameters >= 2) {
        crash.fault_kind = (int)r->ExceptionInformation[0];
        crash.fault_addr = (uintptr_t)r->ExceptionInformation[1];
    }
    crash.thread = ico_sched_current();
    stack_bounds(crash.sp, &crash.stack_lo, &crash.stack_hi);
    crash_common();
}

ICO_ENTRY static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    uintptr_t a = (uintptr_t)r->ExceptionAddress;
    int ours = in_exe(a);
    if (!ours) {
        /* the C runtime the game calls (memcpy, strcmp, ...) */
        HMODULE m = NULL;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)a, &m) &&
            GetModuleFileNameA(m, path, sizeof path) > 0) {
            const char *b = strrchr(path, '\\');
            b = b != NULL ? b + 1 : path;
            ours = _strnicmp(b, "msvcrt", 6) == 0 || _strnicmp(b, "ucrtbase", 8) == 0;
        }
    }
    if (fatal_code(r->ExceptionCode) && (ours || r->ExceptionCode == EXCEPTION_STACK_OVERFLOW)) {
        crash_from_exception(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

ICO_ENTRY static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    crash_from_exception(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

ICO_ENTRY static void abort_handler(int sig)
{
    volatile char here = 0;
    (void)sig;
    if (InterlockedExchange((volatile LONG *)&fatal_once, 1) != 0) {
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    crash.what = "abort() (an assertion or a fatal error; see the last failure message)";
    crash.code = SIGABRT;
    crash.pc = 0;
    crash.sp = (uintptr_t)&here;
    crash.fault_kind = -1;
    crash.thread = ico_sched_current();
    stack_bounds(crash.sp, &crash.stack_lo, &crash.stack_hi);
    crash_common();
}

static void install_handlers(void)
{
    SetUnhandledExceptionFilter(unhandled_filter);
    signal(SIGABRT, abort_handler);
    /* no "this program has stopped working" box on top of ours */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

static void exe_range(void)
{
    HMODULE m = GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)m;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)m + dos->e_lfanew);
    char path[MAX_PATH];
    exe_lo = (uintptr_t)m;
    exe_hi = exe_lo + nt->OptionalHeader.SizeOfImage;
    if (GetModuleFileNameA(m, path, sizeof path) > 0) {
        const char *b = strrchr(path, '\\');
        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : path);
    }
}

#else

static char alt_stack[64 * 1024];

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL (a trap: __builtin_trap / ICO_BREAK)";
    case SIGFPE:
        return "SIGFPE";
    case SIGTRAP:
        return "SIGTRAP";
    case SIGABRT:
        return "abort() (an assertion or a fatal error; see the last failure message)";
    }
    return "a signal";
}

#ifdef __ANDROID__

static void restore_handlers(void);

#endif

ICO_ENTRY static void crash_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    if (fatal_once++) {
        _exit(EXIT_CRASH);
    }
    crash.what = signal_name(sig);
    crash.code = (unsigned long)sig;
#if defined(__x86_64__)
    crash.pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    crash.sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
    crash.pc = (uintptr_t)uc->uc_mcontext.pc;
    crash.sp = (uintptr_t)uc->uc_mcontext.sp;
#else
    (void)uc;
#endif
    crash.fault_kind = -1;
    if (sig == SIGSEGV || sig == SIGBUS) {
        crash.fault_kind = 0;
        crash.fault_addr = (uintptr_t)si->si_addr;
    }
    crash.thread = ico_sched_current();
    stack_bounds_here(crash.sp, &crash.stack_lo, &crash.stack_hi);
#ifdef __ANDROID__
    /* v0.4.2: the C library's last words first (abort, FORTIFY and the
       stack protector write them to stderr, the log mirror's pipe) */
    fatal_ui_flush();
    report_crash_block();
    flog("ico_pc: the run ended: %s", crash.what);
    if (exit_hook != NULL) {
        exit_hook(crash.what);
    }
    {
        /* v0.4.2: a box instead of the game just going (the system shows
           nothing), from the watchdog thread; this thread waits for it */
        static char box[1536];
        fatal_text(box, sizeof box, "ICO ran into a problem and has to close.");
        fatal_ui_box(box);
    }
    flog("ico_pc: the system's crash report (tombstone) follows");
    /* the system's handlers back; a fault repeats when this returns, a
       signal sent by a call (abort, kill) is sent again, pending until
       then */
    restore_handlers();
    if (si->si_code <= 0) {
        raise(sig);
    }
#else
    report_crash();
#endif
}

#ifdef __ANDROID__

/* libmain.so's extent: its executable segments, from its load address */
static int module_phdr(struct dl_phdr_info *info, size_t size, void *user)
{
    uintptr_t base = *(const uintptr_t *)user;
    uintptr_t hi = 0;
    int i;

    (void)size;
    if ((uintptr_t)info->dlpi_addr != base) {
        return 0;
    }
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];

        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) != 0) {
            uintptr_t end = base + (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_memsz;

            if (end > hi) {
                hi = end;
            }
        }
    }
    exe_hi = hi;
    return 1;
}

static void exe_range(void)
{
    Dl_info info;
    uintptr_t base;

    if (dladdr((void *)ico_diag_init, &info) == 0 || info.dli_fbase == NULL) {
        return;
    }
    base = (uintptr_t)info.dli_fbase;
    exe_lo = base;
    dl_iterate_phdr(module_phdr, &base);
    if (info.dli_fname != NULL) {
        const char *b = strrchr(info.dli_fname, '/');

        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : info.dli_fname);
    }
}

#else

extern char __executable_start[] __attribute__((weak));
extern char etext[] __attribute__((weak));

static void exe_range(void)
{
    char path[512];
    ssize_t n;
    if (__executable_start != NULL && etext != NULL) {
        exe_lo = (uintptr_t)__executable_start;
        exe_hi = (uintptr_t)etext;
    }
    n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) {
        const char *b;
        path[n] = '\0';
        b = strrchr(path, '/');
        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : path);
    }
}

#endif

static const int crash_sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};

#ifdef __ANDROID__

/* the handlers in place before ours (the system's crash reporter) */
static struct sigaction old_actions[sizeof crash_sigs / sizeof crash_sigs[0]];

static void restore_handlers(void)
{
    size_t i;

    for (i = 0; i < sizeof crash_sigs / sizeof crash_sigs[0]; i++) {
        sigaction(crash_sigs[i], &old_actions[i], NULL);
    }
}

#endif

static void install_handlers(void)
{
    const int *sigs = crash_sigs;
    struct sigaction sa;
    stack_t st;
    pthread_attr_t attr;
    size_t i;

    main_thread = pthread_self();
    if (pthread_getattr_np(main_thread, &attr) == 0) {
        void *addr;
        size_t size;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
            main_stack_lo = (uintptr_t)addr;
            main_stack_hi = (uintptr_t)addr + size;
        }
        pthread_attr_destroy(&attr);
    }
    memset(&st, 0, sizeof st);
    st.ss_sp = alt_stack;
    st.ss_size = sizeof alt_stack;
    sigaltstack(&st, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof crash_sigs / sizeof crash_sigs[0]; i++) {
#ifdef __ANDROID__
        sigaction(sigs[i], &sa, &old_actions[i]);
#else
        sigaction(sigs[i], &sa, NULL);
#endif
    }
    sa.sa_sigaction = sample_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);
}

#endif

void ico_diag_init(const char *log_path)
{
    if (inited) {
        return;
    }
#ifdef _WIN32
    t0 = GetTickCount64();
    w32_threads();
    InitializeCriticalSection(&lock);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0,
                    FALSE, DUPLICATE_SAME_ACCESS);
    main_tid = GetCurrentThreadId();
    crash_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    crash_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (log_path != NULL) {
        /* a UTF-8 path (host_fs.h) */
        wchar_t *wp = ico_widen(log_path);

        if (wp != NULL) {
            log_h = CreateFileW(wp, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            free(wp);
        } else {
            log_h = CreateFileA(log_path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
    }
#else
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (log_path != NULL) {
        log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    }
#endif
    if (log_path != NULL) {
        snprintf(log_path_copy, sizeof log_path_copy, "%s", log_path);
    }
    exe_range();
    install_handlers();
    main_alive = 1;
    inited = 1;
#ifdef __ANDROID__
    ico_diag_log("ico_pc: diagnostics: %s at 0x%lx; this thread's stack 0x%lx..0x%lx (%lu KB)",
                 exe_name, (unsigned long)exe_lo, (unsigned long)main_stack_lo,
                 (unsigned long)main_stack_hi,
                 (unsigned long)((main_stack_hi - main_stack_lo) / 1024));
#endif
}

#ifdef _WIN32

/* The vectored handler goes in once the file dialog is behind us (shell
   extensions in it may raise and handle their own faults). */
void ico_diag_arm_vectored(void);

void ico_diag_arm_vectored(void)
{
    AddVectoredExceptionHandler(1, vectored_handler);
}

#endif
